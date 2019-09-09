#include <stdint.h>
#include <string.h>

#include "raat.hpp"
#include "raat-buffer.hpp"

#include "raat-oneshot-timer.hpp"
#include "raat-oneshot-task.hpp"
#include "raat-task.hpp"

#include "raat-debouncer.hpp"

#include "http-get-server.hpp"

static HTTPGetServer s_server(true);

static const raat_devices_struct * pDevices;
static const raat_params_struct * pParams;

static void send_standard_erm_response()
{
    s_server.set_response_code_P(PSTR("200 OK"));
    s_server.set_header_P(PSTR("Access-Control-Allow-Origin"), PSTR("*"));
    s_server.finish_headers();
}

static void raise(char const * const url)
{
    raat_logln_P(LOG_APP, PSTR("Raise..."));
    pDevices->pWinch->set(true);

    if (url)
    {
        send_standard_erm_response();
    }
}

static void stop(char const * const url)
{
    raat_logln_P(LOG_APP, PSTR("Stop..."));
    pDevices->pWinch->set(false);

    if (url)
    {
        send_standard_erm_response();
    }
}


static void set_target(char const * const url)
{
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

static void tare(char const * const url)
{
    pDevices->pScales->tare();
    if (url)
    {
        send_standard_erm_response();
    }
}

static void open(char const * const url)
{
    pDevices->pMaglock->set(false);
    if (url)
    {
        send_standard_erm_response();
    }
}

static const char RAISE_URL[] PROGMEM = "/raise";
static const char STOP_URL[] PROGMEM = "/stop";
static const char SET_TARGET_URL[] PROGMEM = "/set_target";
static const char TARE_URL[] PROGMEM = "/tare";
static const char OPEN_URL[] PROGMEM = "/open";

static http_get_handler s_handlers[] = 
{
    {RAISE_URL, raise},
    {STOP_URL, stop},
    {SET_TARGET_URL, set_target},
    {TARE_URL, tare},
    {OPEN_URL, open},
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
        return (current_weight <= (long)max_weight) && (current_weight >= (long)min_weight);
    }
    else
    {
        return false;
    }
}
static RAATDebouncer s_weight_debouncer(weight_reader_fn, 10);

static void weight_trigger_task_fn(RAATTask& ThisTask, void * pTaskData)
{
    (void)ThisTask;
    (void)pTaskData;
    
    s_weight_debouncer.tick();

    if (s_weight_debouncer.check_high_and_clear())
    {
        raat_logln_P(LOG_APP, PSTR("Weight trigger!"));
        raise(NULL);
    }
    else if (s_weight_debouncer.check_low_and_clear())
    {
        raat_logln_P(LOG_APP, PSTR("Weight untrigger!"));
        stop(NULL);
    }
}
static RAATTask s_weight_trigger_task(100, weight_trigger_task_fn, NULL);

static void debug_task_fn(RAATTask& ThisTask, void * pTaskData)
{
    long current_weight;
    if (pDevices->pScales->get_scaled(current_weight))
    {
        raat_logln_P(LOG_APP, PSTR("Weight: %" PRIu32), current_weight);    
    }
}
static RAATTask s_debug_task(2000, debug_task_fn, NULL);

void raat_custom_setup(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)params;
    pDevices = &devices;
    pParams = &params;

    devices.pMaglock->set(true);
}

void raat_custom_loop(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)devices; (void)params;
    s_weight_trigger_task.run();
    s_debug_task.run();
    if (devices.pSet_Target_Weight->check_low_and_clear())
    {
        set_target(NULL);
    }

    if (devices.pTare->check_low_and_clear())
    {
        tare(NULL);
    }


}
