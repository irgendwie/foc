local L4 = require "L4"

local t = require("rom/test_env")
local l = L4.default_loader

local devcap = l:new_channel()
local drvcap = l:new_channel()

--------- Proxy

l:start({caps = {
          dev = devcap:m("rw"),
          drv = drvcap:svr(),
        }},
        "rom/virtio-trusty-proxy -vvv")

--------- Test

l:start({caps = {
          devcap = devcap:svr(),
          drvcap = drvcap:m("rw"),
        }},
        "rom/" .. t.TEST_PROG)

