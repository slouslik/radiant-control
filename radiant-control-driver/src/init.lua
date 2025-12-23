local capabilities = require "st.capabilities"
local ZigbeeDriver = require "st.zigbee"
local defaults = require "st.zigbee.defaults"
local clusters = require "st.zigbee.zcl.clusters"
local log = require "log"

-- Capability IDs
local CAP_MIX_TEMP = "cablecenter03358.mixTemperature"
local CAP_RETURN_TEMP = "cablecenter03358.returnTemperature"
local CAP_MIX_POS = "cablecenter03358.mixingValvePosition"
local CAP_MIX_SETPOINT = "cablecenter03358.mixingValveSetpoint"

-- Parse and emit Arduino data
local function parse_and_emit(device, text)
  if not text or text == "" then return end
  
  log.info("Arduino message: " .. text)
  
  local parts = {}
  for word in text:gmatch("%S+") do 
    table.insert(parts, word) 
  end
  
  local name = parts[1]
  local value = tonumber(parts[2])
  
  if not name and not value then 
    log.warn("Could not parse: " .. text)
    return 
  end
  
  if name == "temperature" then
    log.info("Temp: " .. value)
  elseif name == "mixTemp" then
    device:emit_event(capabilities[CAP_MIX_TEMP].mixTemp({value = value, unit = "F"}))
  elseif name == "mixValvePosition" then
    device:emit_event(capabilities[CAP_MIX_POS].mixValvePosition({value = value}))
  elseif name == "mixSetpoint" then
    device:emit_event(capabilities[CAP_MIX_SETPOINT].mixSetpoint({value = value}))
  elseif name == "returnTemp" then
    device:emit_event(capabilities[CAP_RETURN_TEMP].returnTemp({value = value, unit = "F"}))
  elseif name == "ping" then
    log.info("Ping received")
  end
end

-- Attribute handler for ProductCode (0x000A)
local function arduino_text_handler(driver, device, value, zb_rx)
  log.info("=== arduino_text_handler called ===")
  
  -- Extract the string value
  local msg = nil
  if type(value) == "table" and value.value then
    msg = value.value
  elseif type(value) == "string" then
    msg = value
  else
    msg = tostring(value)
  end
  
  parse_and_emit(device, msg)
end

-- Global handler to catch malformed messages
local function generic_handler(driver, device, zb_rx)
  log.info("=== generic_handler called ===")
  
  -- Print the readable summary
  log.debug("Full zb_rx message: " .. tostring(zb_rx))
  
  -- Correct way to get raw hex bytes from the GenericBody
  local bytes = zb_rx.body.zcl_body.body_bytes
  if not bytes then
     log.debug("No GenericBody!")
     return
  end

  log.debug(string.format("Raw Hex Body: %s", bytes:gsub(".", function (c) return string.format("%02X ", string.byte(c)) end)))
  local len = string.len(bytes)
  
  log.info("Processing GenericBody, length: " .. len)
  
  -- Extract ASCII text from bytes
  -- Format: [FC:1][SEQ:1][CMD:1][ATTR_ID:2][DATA_TYPE:1][LEN:1][STRING...]
  -- Skip first 7 bytes to get to the string data
  local text = ""
  for i = 8, len do
    local b = string.byte(bytes, i)
    if b and b >= 32 and b <= 126 then
      text = text .. string.char(b)
    end
  end
  
  if #text > 0 then
    log.info("Extracted text: " .. text)
    parse_and_emit(device, text)
  else
    log.warn("No text extracted from GenericBody")
  end
end

-- Command: Set mix temp
local function handle_set_mix_setpoint(driver, device, command)
  local value = command.args.value
  log.info("Setting mix setpoint: " .. value)
  
  local payload = "mixSetpoint " .. tostring(value)
  local write_cmd = clusters.Basic.attributes.LocationDescription:write(device, payload)
  device:send(write_cmd)
  device:emit_event(capabilities[CAP_MIX_SETPOINT].mixSetpoint({value = value}))
end

-- Driver template
local radiant_driver = {
  supported_capabilities = {
    capabilities[CAP_MIX_TEMP],
    capabilities[CAP_RETURN_TEMP],
    capabilities[CAP_MIX_POS],
    capabilities[CAP_MIX_SETPOINT],
    capabilities.refresh
  },
  capability_handlers = {
    [capabilities.refresh.ID] = {
      [capabilities.refresh.commands.refresh.NAME] = function(driver, device)
        log.info("Refresh triggered")
	local write_cmd = clusters.Basic.attributes.LocationDescription:write(device, "refresh")
	device:send(write_cmd)
      end
    },
    [capabilities[CAP_MIX_SETPOINT].ID] = {
      [capabilities[CAP_MIX_SETPOINT].commands.setMixSetpoint.NAME] = handle_set_mix_setpoint
    }
  },
  zigbee_handlers = {
    global = {
      [clusters.Basic.ID] = {
        [0xFF] = generic_handler
      }
    },
    attr = {
      [clusters.Basic.ID] = {
        [clusters.Basic.attributes.ProductCode.ID] = arduino_text_handler
      }
    }
  }
}

defaults.register_for_default_handlers(radiant_driver, radiant_driver.supported_capabilities)
local driver = ZigbeeDriver("radiant-control", radiant_driver)
driver:run()
