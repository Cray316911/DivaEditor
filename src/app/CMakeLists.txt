qt_add_executable(DivaStudio main.cpp)
set_target_properties(DivaStudio PROPERTIES WIN32_EXECUTABLE TRUE)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(DivaStudio
    URI DivaStudio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Glyph.qml
        qml/IconButton.qml
        qml/DButton.qml
        qml/Choice.qml
        qml/Toggle.qml
        qml/DSlider.qml
        qml/NeonFrame.qml
        qml/AppRail.qml
        qml/InfoCard.qml
        qml/WelcomeView.qml
        qml/EditorView.qml
        qml/DivaAIPanel.qml
        qml/RecordSetupDialog.qml
        qml/RegionSelector.qml
        qml/CountdownWindow.qml
        qml/RecordingBar.qml
        qml/ExportDialog.qml
        qml/ApplyEditsDialog.qml
    RESOURCES
        fonts/Poppins-Regular.ttf
        fonts/Poppins-Medium.ttf
        fonts/Poppins-SemiBold.ttf
        fonts/Poppins-Bold.ttf
    SOURCES
        VideoItem.cpp VideoItem.h
        ProjectController.cpp ProjectController.h
        RecorderController.cpp RecorderController.h
        AgentController.cpp AgentController.h
        SystemInfo.cpp SystemInfo.h
)

target_link_libraries(DivaStudio PRIVATE divaedit divacapture Qt6::Quick Qt6::QuickControls2 Qt6::Network)
diva_copy_ffmpeg_dlls(DivaStudio)

# Put the Qt runtime next to the exe so it starts from the build folder.
find_program(WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
if(WINDEPLOYQT)
    add_custom_command(TARGET DivaStudio POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime --no-opengl-sw
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:DivaStudio>"
        COMMENT "windeployqt DivaStudio"
        VERBATIM)
endif()
