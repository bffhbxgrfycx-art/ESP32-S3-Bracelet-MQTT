idf_component_register(
                         SRCS "main.c"
                              "mqtt_app.c"
                              "watch_ui.c"
                              "LCD_Driver/Vernon_ST7789T/Vernon_ST7789T.c"
                              "LCD_Driver/ST7789.c"
                              "LVGL_Driver/LVGL_Driver.c"
                              "BAT_Driver/BAT_Driver.c"

                         INCLUDE_DIRS
                              "."
                              "./LCD_Driver/Vernon_ST7789T"
                              "./LCD_Driver"
                              "./LVGL_Driver"
                              "./BAT_Driver"
                       )
