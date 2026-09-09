idf_component_register(
                         SRCS "main.c"
                              "mqtt_app.c"
                              "watch_ui.c"
                              "provisioning.c"
                              "LCD_Driver/Vernon_ST7789T/Vernon_ST7789T.c"
                              "LCD_Driver/ST7789.c"
                              "LVGL_Driver/LVGL_Driver.c"
                              "BAT_Driver/BAT_Driver.c"
                              "Button_Driver/multi_button.c"
                              "Button_Driver/Button_Driver.c"
                              "I2C_Driver/I2C_Driver.c"
                              "QMI8658/QMI8658.c"

                         INCLUDE_DIRS
                              "."
                              "./LCD_Driver/Vernon_ST7789T"
                              "./LCD_Driver"
                              "./LVGL_Driver"
                              "./BAT_Driver"
                              "./Button_Driver"
                              "./I2C_Driver"
                              "./QMI8658"
                       )

# 源文件用 UTF-8 编码：Windows 中文系统 GCC 默认按 GBK 读源文件，
# 会导致中文注释/字符串乱码、snprintf 格式串误判。这里强制按 UTF-8 读写。
target_compile_options(${COMPONENT_LIB} PRIVATE
                       "-finput-charset=UTF-8"
                       "-fexec-charset=UTF-8")
