#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(pmw3610_test, LOG_LEVEL_DBG);

static const struct device *const sensor = DEVICE_DT_GET(DT_NODELABEL(trackball));
static atomic_t motion_reports;
static atomic_t motion_interrupts;
static const struct gpio_dt_spec motion_gpio =
    GPIO_DT_SPEC_GET(DT_NODELABEL(trackball), motion_gpios);
static struct gpio_callback motion_observer;
static int observer_status;
static struct k_work_delayable heartbeat;

/* Observe the driver's existing interrupt without changing its configuration. */
static void motion_irq_observed(const struct device *port,
                                struct gpio_callback *callback, uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);
    atomic_inc(&motion_interrupts);
}

static void motion_event(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);
    LOG_DBG("PMW3610 input: type=%u code=%u value=%d sync=%u",
            evt->type, evt->code, evt->value, evt->sync);
    if (evt->sync) {
        atomic_inc(&motion_reports);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackball)), motion_event, NULL);

static void heartbeat_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    int raw_motion = gpio_is_ready_dt(&motion_gpio) ?
                     gpio_pin_get_raw(motion_gpio.port, motion_gpio.pin) : -ENODEV;

    LOG_INF("PMW3610: ready=%d MOTION(P0.20)=%d (0=asserted) IRQs=%ld reports=%ld observer=%d",
            device_is_ready(sensor), raw_motion,
            (long)atomic_get(&motion_interrupts),
            (long)atomic_get(&motion_reports), observer_status);
    k_work_reschedule(&heartbeat, K_SECONDS(5));
}

static int diagnostics_init(void)
{
    LOG_INF("Right controller: standalone USB trackball test; no physical keys");
    LOG_INF("PMW3610 wiring: SCK=P0.29 SDIO=P0.02 CS=P0.31 MOTION=P0.20 RESET=P0.22");
    if (gpio_is_ready_dt(&motion_gpio)) {
        gpio_init_callback(&motion_observer, motion_irq_observed, BIT(motion_gpio.pin));
        observer_status = gpio_add_callback(motion_gpio.port, &motion_observer);
    } else {
        observer_status = -ENODEV;
    }
    LOG_INF("Motion IRQ observer status=%d (0=attached); no interrupt reconfiguration",
            observer_status);
    if (device_is_ready(sensor)) {
        LOG_INF("PMW3610 ready: driver initialization passed product ID (0x3e), observation and motion IRQ setup");
    } else {
        LOG_ERR("PMW3610 NOT READY: inspect input_pmw3610 and SPI errors; check power, ground and signal wiring");
    }
    k_work_init_delayable(&heartbeat, heartbeat_handler);
    k_work_schedule(&heartbeat, K_SECONDS(10));
    return 0;
}

SYS_INIT(diagnostics_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
