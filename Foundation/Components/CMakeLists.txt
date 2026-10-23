include("${CMAKE_CURRENT_LIST_DIR}/../ParallelBuild.cmake")

# Foundation component subdirectories are added here explicitly when their
# private runtime implementations are introduced. Keeping this list explicit
# makes component load order and build ownership reviewable.
