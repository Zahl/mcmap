#include "./mcmap.h"

#ifdef _OPENMP
#define THREADS omp_get_max_threads()
#else
#define THREADS 1
#endif

int render(const Settings::WorldOptions &options,
           const Colors::Palette &colors) {
  logger::debug("Rendering {} with {}\n", options.save.name,
                options.boundaries.to_string());

  // Split terrain according to tile size
  std::vector<Terrain::Coordinates> tiles;
  options.boundaries.tile(tiles, options.tile_size);

  std::vector<Canvas> fragments(tiles.size());

  // This value represents the amount of canvasses that can fit in memory at
  // once to avoid going over the limit of RAM
  Counter<size_t> capacity = memory_capacity(
      options.mem_limit, tiles[0].footprint(), tiles.size(), THREADS);

  if (!capacity)
    return false;

  logger::debug("Memory capacity: {} tiles - {} tiles scheduled\n",
                size_t(capacity), tiles.size());

  // If caching is needed, ensure the cache directory is available
  if (capacity < tiles.size())
    if (!prepare_cache(CACHE))
      return false;

#ifdef _OPENMP
#pragma omp parallel shared(fragments, capacity)
#endif
  {
#ifdef _OPENMP
#pragma omp for ordered schedule(dynamic)
#endif
    for (std::vector<Terrain::Coordinates>::size_type i = 0; i < tiles.size();
         i++) {
      IsometricCanvas canvas;
      canvas.setMap(tiles[i]);
      canvas.setColors(colors);

      // Load the minecraft terrain to render
      Terrain::Data world(tiles[i]);
      world.load(options.regionDir());

      // Cap the height to avoid having a ridiculous image height
      tiles[i].minY = std::max(tiles[i].minY, world.minHeight());
      tiles[i].maxY = std::min(tiles[i].maxY, world.maxHeight());

      // Draw the terrain fragment
      canvas.shading = options.shading;
      // canvas.setMarkers(options.totalMarkers, &options.markers);
      canvas.renderTerrain(world);

      if (!canvas.empty()) {
        if (i >= capacity) {
          std::filesystem::path temporary =
              fmt::format("{}/{}.png", CACHE, canvas.map.to_string());
          canvas.save(temporary);

          fragments[i] = std::move(ImageCanvas(canvas.map, temporary));
        } else
          fragments[i] = std::move(canvas);
      } else {
        // If the canvas was empty, increase the capacity to reflect the free
        // space
        if (i < capacity)
          ++capacity;
      }
    }
  }

  CompositeCanvas merged(std::move(fragments));
  logger::debug("{}\n", merged.to_string());

  if (merged.empty()) {
    logger::error("Canvas is empty !\n");
    return false;
  }

  if (!merged.save(options.outFile, options.padding))
    return false;

  logger::info("Job complete.\n");
  return true;
}