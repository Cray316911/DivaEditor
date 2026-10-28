# divaedit: project model, timeline renderer and DivaAI's editing tools.
add_library(divaedit STATIC
    Project.cpp Project.h
    AudioReader.cpp AudioReader.h
    FrameSource.cpp FrameSource.h
    TransitionRenderer.cpp TransitionRenderer.h
    TimelineRenderer.cpp TimelineRenderer.h
    AgentTools.cpp AgentTools.h
)
target_include_directories(divaedit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(divaedit PUBLIC divacore Qt6::Core Qt6::Gui)
