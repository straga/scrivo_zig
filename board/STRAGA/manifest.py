freeze("$(PORT_DIR)/modules")
include("$(MPY_DIR)/extmod/asyncio")

#network
require("ntptime")
require("requests")
require("urequests")
require("ssl")

#other
require("upysh")

# My freeze
freeze('$(PORT_DIR)/boards/STRAGA/modules/safe_lib')