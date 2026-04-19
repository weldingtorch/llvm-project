if (NOT WIN32 AND NOT CYGWIN)
  add_llvm_pass_plugin(Duviz
    DefUseViz.cpp
    PARTIAL_SOURCES_INTENDED
    DEPENDS
    intrinsics_gen
    BUILDTREE_ONLY
  )
  target_link_libraries(Duviz PRIVATE gvc)
  target_link_libraries(Duviz PRIVATE cgraph)
  target_link_libraries(Duviz PRIVATE cdt)
  add_library(logger STATIC logger.c)
endif()
