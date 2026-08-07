juce_add_console_app(PabloTests PRODUCT_NAME "PabloTests")

target_sources(PabloTests PRIVATE
    ${PABLO_CORE_SOURCES}
    Tests.cpp
)

pablo_configure_common(PabloTests)

target_link_libraries(PabloTests
    PRIVATE
        ${PABLO_CORE_JUCE_LIBS}
        juce::juce_recommended_config_flags
)

add_test(NAME PabloTests COMMAND PabloTests)
