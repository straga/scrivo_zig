set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base                           
    boards/sdkconfig.usb                         
    boards/sdkconfig.ble                            
    boards/sdkconfig.spiram_sx
    boards/STRAGA/sdkconfig.board                   #straga board                     
    ${MICROPY_BOARD_DIR}/sdkconfig.board
)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

