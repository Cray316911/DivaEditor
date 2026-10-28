# MCP server exposing DivaAI's editing tools to Claude Code (stdio transport).
add_executable(divamcp main.cpp)
target_link_libraries(divamcp PRIVATE divaedit Qt6::Gui)
diva_copy_ffmpeg_dlls(divamcp)
