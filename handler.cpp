#include <stdint.h>
#include <string.h>

#include "raat.hpp"
#include "raat-buffer.hpp"

#include "raat-oneshot-timer.hpp"
#include "raat-oneshot-task.hpp"
#include "raat-task.hpp"

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
    pDevices->pRelay1->set(true);
    pDevices->pRelay2->set(false);

    if (url)
    {
        send_standard_erm_response();
    }
}

static void lower(char const * const url)
{
    raat_logln_P(LOG_APP, PSTR("Lowering..."));
    pDevices->pRelay1->set(false);
    pDevices->pRelay2->set(true);

    if (url)
    {
        send_standard_erm_response();
    }
}

static void stop(char const * const url)
{
    raat_logln_P(LOG_APP, PSTR("Stop..."));
    pDevices->pRelay1->set(false);
    pDevices->pRelay2->set(false);

    if (url)
    {
        send_standard_erm_response();
    }
}


static void set_target(char const * const url)
{
    long current_weight;
    if (pDevices->pScales->get(current_weight))
    {
        raat_logln_P(LOG_APP, PSTR("Resetting target weight to %" PRIu32), current_weight);
        pParams->pTarget_Weight->set((uint32_t)current_weight);
    }

    if (url)
    {
        send_standard_erm_response();
    }
}

static const char RAISE_URL[] PROGMEM = "/raise";
static const char LOWER_URL[] PROGMEM = "/lower";
static const char STOP_URL[] PROGMEM = "/stop";
static const char SET_TARGET_URL[] PROGMEM = "/set_target";

static http_get_handler s_handlers[] = 
{
    {RAISE_URL, raise},
    {LOWER_URL, lower},
    {STOP_URL, stop},
    {SET_TARGET_URL, set_target},
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

    if (pDevices->pScales->get(current_weight))
    {
        return (current_weight <= max_weight) && (current_weight >= min_weight);
    }
    else
    {
        return false;
    }
}
static RAATDebouncer s_weight_debouncer(weight_reader_fn, 10);

static void weight_trigger_task_fn(RAATOneShotTask& pThisTask, void * pTaskData)
{
    s_weight_debouncer.tick();

    if (s_weight_debouncer.check_high_and_clear())
    {
        raat_logln_P(LOG_APP, PSTR("Weight trigger!"));
        lower(NULL);
    }
}
static RAATTask s_weight_trigger_task(100, weight_trigger_task_fn, NULL);


void raat_custom_setup(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)params;
    pDevices = &devices;
    pParams = &params;
}

void raat_custom_loop(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)devices; (void)params;
    s_weight_trigger_task.run();

    if (devices.pSet_Target_Weight->check_low_and_clear())
    {
        set_target(NULL);
    }
}
