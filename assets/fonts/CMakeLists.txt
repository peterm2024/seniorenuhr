idf_component_register(
    SRCS "schrift_uhr_128.c" "schrift_gross_72.c" "schrift_mittel_40.c" "schrift_klein_28.c"
    REQUIRES lvgl__lvgl
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
