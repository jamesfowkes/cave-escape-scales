#include <stdint.h>
#include <string.h>

#include "raat.hpp"
#include "raat-buffer.hpp"

#include "raat-oneshot-timer.hpp"
#include "raat-oneshot-task.hpp"
#include "raat-task.hpp"

#include "raat-debouncer.hpp"

#include "http-get-server.hpp"

typedef struct _timeout
{
    int32_t time;
    bool active;
} timeout;

static timeout s_lower_door_timeout;

static HTTPGetServer s_server(NULL);

static const raat_devices_struct * pDevices;
static const raat_params_struct * pParams;

static bool s_bOverride = false;
static bool s_bGameComplete = false;

static void send_standard_erm_response()
{
    s_server.set_response_code_P(PSTR("200 OK"));
    s_server.set_header_P(PSTR("Access-Control-Allow-Origin"), PSTR("*"));
    s_server.finish_headers();
}

static void raise(char const * const url, char const * const end)
{
    (void)end;
    if (!s_bOverride)
    {
        raat_logln_P(LOG_APP, PSTR("Raise..."));
    }

    pDevices->pWinch->set(true);

    if (url)
    {
        send_standard_erm_response();
    }
}

static void stop(char const * const url, char const * const end)
{
    (void)end;
    raat_logln_P(LOG_APP, PSTR("Stop..."));
    pDevices->pWinch->set(false);

    if (url)
    {
        send_standard_erm_response();
    }
}

static void set_target(char const * const url, char const * const end)
{
    (void)end;
    long current_weight;
    if (pDevices->pScales->get_scaled(current_weight))
    {
        raat_logln_P(LOG_APP, PSTR("Resetting target weight to %" PRIu32), current_weight);
        pParams->pTarget_Weight->set((uint32_t)current_weight);
    }

    if (url)
    {
        send_standard_erm_response();
    }
}

static void tare(char const * const url, char const * const end)
{
    (void)end;
    pDevices->pScales->tare();
    if (url)
    {
        send_standard_erm_response();
    }
}

static void open_door(char const * const url, char const * const end)
{
    (void)end;
    pDevices->pMaglock->set(false);
    if (url)
    {
        send_standard_erm_response();
    }
}

static void close_door(char const * const url, char const * const end)
{
    (void)end;
    pDevices->pMaglock->set(true);
    if (url)
    {
        send_standard_erm_response();
    }
}

static void open_lower_door(void)
{
    raat_logln_P(LOG_APP, PSTR("Opening lower door"));
    s_lower_door_timeout.active = false;
    pDevices->pMaglock2->set(false);
}

static void close_lower_door(void)
{
    raat_logln_P(LOG_APP, PSTR("Closing lower door"));
    s_lower_door_timeout.active = false;
    pDevices->pMaglock2->set(true);
}

static void start_lower_door_timeout(int32_t timeout)
{
    raat_logln_P(LOG_APP, PSTR("Starting %" PRIi32 " timeout on lower door"), timeout);
    s_lower_door_timeout.time = (timeout / 100) * 100;;
    s_lower_door_timeout.active = true;
}

static void open_lower_door(char const * const url, char const * const end)
{
    (void)url;
    int32_t timeout;

    bool success = false;

    send_standard_erm_response();

    if ((success = raat_parse_single_numeric(end+1, timeout, NULL)))
    {
        success &= timeout > 100;
        if (success)
        {
            open_lower_door();
            start_lower_door_timeout(timeout);
        }
    }
    else
    {
        open_lower_door();
    }

    if (url)
    {
        send_standard_erm_response();
    }
}

static void game_complete_poll(char const * const url, char const * const end)
{
    (void)end;
    if (url)
    {
        send_standard_erm_response();
        s_server.add_body_P(s_bGameComplete ? PSTR("COMPLETE\r\n\r\n") : PSTR("NOT COMPLETE\r\n\r\n"));
    }
}

static void set_scale_factor(char const * const url)
{
    raat_logln_P(LOG_APP, PSTR("Setting scale factor to %d"));
    pDevices->pScales->set_scale(url+7);

    if (url)
    {
        send_standard_erm_response();
    }
}

static void get_weight(char const * const url)
{
    if (url)
    {
        long current_weight = 0;
        char buffer[16];

        if (pDevices->pScales->get_scaled(current_weight))
        {
            sprintf(buffer, "%lu", current_weight);
        }

        send_standard_erm_response();
        s_server.add_body(buffer);
    }
}

static const char RAISE_URL[] PROGMEM = "/raise";
static const char STOP_URL[] PROGMEM = "/stop";
static const char SET_TARGET_URL[] PROGMEM = "/set_target";
static const char TARE_URL[] PROGMEM = "/tare";
static const char OPEN_URL[] PROGMEM = "/open";
static const char CLOSE_URL[] PROGMEM = "/close";
static const char OPEN_LOWER_DOOR_URL[] PROGMEM = "/lower_door/open";
static const char CLOSE_LOWER_DOOR_URL[] PROGMEM = "/lower_door/close";
static const char GAME_COMPLETE_POLL[] PROGMEM = "/game_complete";
static const char SCALE_URL[] PROGMEM = "/scale";
static const char GET_URL[] PROGMEM = "/get";

static http_get_handler s_handlers[] =
{
    {RAISE_URL, raise},
    {STOP_URL, stop},
    {SET_TARGET_URL, set_target},
    {TARE_URL, tare},
    {OPEN_URL, open_door},
    {CLOSE_URL, close_door},
    {OPEN_LOWER_DOOR_URL, open_lower_door},
    {CLOSE_LOWER_DOOR_URL, close_lower_door},
    {GAME_COMPLETE_POLL, game_complete_poll},
    {SCALE_URL, set_scale_factor},
    {GET_URL, get_weight},
    {"", NULL}
};

void ethernet_packet_handler(char * req)
{
    s_server.handle_req(s_handlers, req);
}

char * ethernet_response_provider()
{
    return s_server.get_response();
}

static bool weight_reader_fn()
{
    long current_weight;

    uint32_t target_weight = pParams->pTarget_Weight->get();
    uint32_t min_weight = target_weight - pParams->pWeight_Window->get();
    uint32_t max_weight = target_weight + pParams->pWeight_Window->get();

    if (pDevices->pScales->get_scaled(current_weight))
    {
        return ((uint32_t)current_weight <= max_weight) && ((uint32_t)current_weight >= min_weight);
    }
    else
    {
        return false;
    }
}
static RAATDebouncer s_weight_debouncer(weight_reader_fn, 10);

static void weight_trigger_task_fn(
    __attribute__((unused)) RAATTask& pThisTask, __attribute__((unused)) void * pTaskData
)
{
    (void)ThisTask;
    (void)pTaskData;

    s_weight_debouncer.tick();

    if (s_weight_debouncer.check_high_and_clear())
    {
        raat_logln_P(LOG_APP, PSTR("Weight trigger!"));
        s_bGameComplete = true;
        raise(NULL, NULL);
    }
    else if (s_weight_debouncer.check_low_and_clear())
    {
        raat_logln_P(LOG_APP, PSTR("Weight untrigger!"));
        stop(NULL, NULL);
    }
}
static RAATTask s_weight_trigger_task(100, weight_trigger_task_fn, NULL);

static void debug_task_fn(RAATTask& ThisTask, void * pTaskData)
{
    (void)ThisTask; (void)pTaskData;
    long current_weight;
    if (pDevices->pScales->get_scaled(current_weight))
    {
        raat_logln_P(LOG_APP, PSTR("Weight: %" PRIu32), current_weight);
    }
}
static RAATTask s_debug_task(2000, debug_task_fn, NULL);

static void timeout_task_fn(RAATTask& task, void * pTaskData)
{
    (void)task; (void)pTaskData;
    if (s_lower_door_timeout.active && s_lower_door_timeout.time > 0)
    {
        s_lower_door_timeout.time -= 100;
        if (s_lower_door_timeout.time == 0)
        {
            raat_logln_P(LOG_APP, PSTR("Timeout finish"));
            close_lower_door();
        }
    }
}
static RAATTask s_timeout_task(100, timeout_task_fn);


void raat_custom_setup(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)params;
    pDevices = &devices;
    pParams = &params;

    devices.pMaglock->set(true);
    devices.pMaglock2->set(true);

    raat_logln_P(LOG_APP, PSTR("Target weight: %" PRIu32), pParams->pTarget_Weight->get());
    raat_logln_P(LOG_APP, PSTR("Target window: +/-%" PRIu16), pParams->pWeight_Window->get());
}

void raat_custom_loop(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)devices; (void)params;

    s_weight_trigger_task.run();

    s_debug_task.run();
    if (devices.pSet_Target_Weight->check_low_and_clear())
    {
        set_target(NULL, NULL);
    }

    if (devices.pToggle_Override->check_low_and_clear())
    {
        s_bOverride = !s_bOverride;
        raat_logln_P(LOG_APP, PSTR("Override: %s"), s_bOverride ? "on" : "off");

        if (s_bOverride)
        {
            raise(NULL, NULL);
        }
        else
        {
            stop(NULL, NULL);
        }
    }
    s_timeout_task.run();
}
