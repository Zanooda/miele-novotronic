import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

from . import miele_novotronic_ns

DEPENDENCIES = ["text_sensor"]

MieleNovotronicComponent = miele_novotronic_ns.class_(
    'MotorolaLedDriverSniffer', 
    cg.Component, 
    text_sensor.TextSensor
)

CONF_TIME_SENSOR = "time_sensor"
CONF_STATE_SENSOR = "state_sensor"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MieleNovotronicComponent),
    cv.Required(CONF_TIME_SENSOR): cv.use_id(text_sensor.TextSensor),
    cv.Required(CONF_STATE_SENSOR): cv.use_id(text_sensor.TextSensor),
})

async def to_code(config):
    time_sens = await cg.get_variable(config[CONF_TIME_SENSOR])
    state_sens = await cg.get_variable(config[CONF_STATE_SENSOR])
    
    var = cg.new_Pvariable(config[CONF_ID], time_sens, state_sens)
    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)

