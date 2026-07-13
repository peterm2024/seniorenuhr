cmake_minimum_required(VERSION 3.16)

# Die generierten LVGL-Fonts liegen als eigene Komponente unter assets/fonts
set(EXTRA_COMPONENT_DIRS assets)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(seniorenuhr)
