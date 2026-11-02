import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    DEVICE_CLASS_DISTANCE,
    ICON_ARROW_EXPAND_VERTICAL,
    STATE_CLASS_MEASUREMENT,
    UNIT_MILLIMETER,
)
from esphome.types import ConfigType

CODEOWNERS = ["@LePouet"]
DEPENDENCIES = ["uart"]

a0221at_ns = cg.esphome_ns.namespace("a0221at")
A0221ATComponent = a0221at_ns.class_(
    "A0221ATComponent", sensor.Sensor, cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        A0221ATComponent,
        unit_of_measurement=UNIT_MILLIMETER,
        icon=ICON_ARROW_EXPAND_VERTICAL,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        device_class=DEVICE_CLASS_DISTANCE,
    )
    # Every measurement needs a trigger; the datasheet requires more than
    # 70 ms between two triggers.
    .extend(cv.polling_component_schema("1s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "a0221at",
    baud_rate=9600,
    require_tx=True,  # unlike the auto-output a02yyuw, this variant must be triggered
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config: ConfigType) -> None:
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
