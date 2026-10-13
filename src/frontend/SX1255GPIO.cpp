#include "sx1255/frontend/SX1255GPIO.hpp"

#include <dlfcn.h>

#include <cstring>
#include <vector>

namespace sx1255 {

namespace {

struct gpiod_chip;
struct gpiod_line_settings;
struct gpiod_line_config;
struct gpiod_request_config;
struct gpiod_line_request;

struct GpiodApi {
    bool attempted = false;
    bool loaded = false;
    void* handle = nullptr;

    gpiod_chip* (*chip_open)(const char*) = nullptr;
    void (*chip_close)(gpiod_chip*) = nullptr;
    gpiod_line_settings* (*settings_new)() = nullptr;
    void (*settings_free)(gpiod_line_settings*) = nullptr;
    int (*settings_set_direction)(gpiod_line_settings*, int) = nullptr;
    gpiod_line_config* (*config_new)() = nullptr;
    void (*config_free)(gpiod_line_config*) = nullptr;
    int (*config_add_line_settings)(gpiod_line_config*, const unsigned int*, std::size_t, gpiod_line_settings*) = nullptr;
    gpiod_request_config* (*req_new)() = nullptr;
    void (*req_free)(gpiod_request_config*) = nullptr;
    void (*req_set_consumer)(gpiod_request_config*, const char*) = nullptr;
    gpiod_line_request* (*request_lines)(gpiod_chip*, gpiod_request_config*, gpiod_line_config*) = nullptr;
    void (*release)(gpiod_line_request*) = nullptr;
    int (*set_value)(gpiod_line_request*, unsigned int, int) = nullptr;

    bool load()
    {
        if (loaded) return true;
        if (attempted) return false;
        attempted = true;

        handle = ::dlopen("libgpiod.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            return false;

        chip_open = reinterpret_cast<decltype(chip_open)>(::dlsym(handle, "gpiod_chip_open"));
        chip_close = reinterpret_cast<decltype(chip_close)>(::dlsym(handle, "gpiod_chip_close"));
        settings_new = reinterpret_cast<decltype(settings_new)>(::dlsym(handle, "gpiod_line_settings_new"));
        settings_free = reinterpret_cast<decltype(settings_free)>(::dlsym(handle, "gpiod_line_settings_free"));
        settings_set_direction = reinterpret_cast<decltype(settings_set_direction)>(::dlsym(handle, "gpiod_line_settings_set_direction"));
        config_new = reinterpret_cast<decltype(config_new)>(::dlsym(handle, "gpiod_line_config_new"));
        config_free = reinterpret_cast<decltype(config_free)>(::dlsym(handle, "gpiod_line_config_free"));
        config_add_line_settings = reinterpret_cast<decltype(config_add_line_settings)>(::dlsym(handle, "gpiod_line_config_add_line_settings"));
        req_new = reinterpret_cast<decltype(req_new)>(::dlsym(handle, "gpiod_request_config_new"));
        req_free = reinterpret_cast<decltype(req_free)>(::dlsym(handle, "gpiod_request_config_free"));
        req_set_consumer = reinterpret_cast<decltype(req_set_consumer)>(::dlsym(handle, "gpiod_request_config_set_consumer"));
        request_lines = reinterpret_cast<decltype(request_lines)>(::dlsym(handle, "gpiod_chip_request_lines"));
        release = reinterpret_cast<decltype(release)>(::dlsym(handle, "gpiod_line_request_release"));
        set_value = reinterpret_cast<decltype(set_value)>(::dlsym(handle, "gpiod_line_request_set_value"));

        if (!chip_open || !chip_close || !settings_new || !settings_free ||
            !settings_set_direction || !config_new || !config_free ||
            !config_add_line_settings || !req_new || !req_free ||
            !request_lines || !release || !set_value) {
            ::dlclose(handle);
            handle = nullptr;
            return false;
        }
        loaded = true;
        return true;
    }
};

GpiodApi g_gpiod;

}  // namespace

SX1255Gpiod::SX1255Gpiod(std::string chip, int resetLine, int txEnableLine,
                         int rxEnableLine, int pttLine)
    : m_chip(std::move(chip))
{
    m_lines[kReset] = resetLine;
    m_lines[kTx] = txEnableLine;
    m_lines[kRx] = rxEnableLine;
    m_lines[kPtt] = pttLine;
}

SX1255Gpiod::~SX1255Gpiod()
{
    close();
}

bool SX1255Gpiod::open()
{
    if (m_chipHandle)
        return true;
    if (!g_gpiod.load())
        return false;

    gpiod_chip* chip = g_gpiod.chip_open(m_chip.c_str());
    if (!chip)
        return false;

    m_requestCount = 0;
    for (Index i = kReset; i < kCount; i = static_cast<Index>(i + 1)) {
        if (m_lines[i] >= 0) {
            m_offsets[m_requestCount] = static_cast<unsigned int>(m_lines[i]);
            m_requestCount++;
        }
    }

    if (m_requestCount == 0) {
        m_chipHandle = chip;
        return true;  // no lines configured; still hold the chip
    }

    gpiod_line_settings* ls = g_gpiod.settings_new();
    gpiod_line_config* lc = g_gpiod.config_new();
    gpiod_request_config* rc = g_gpiod.req_new();
    gpiod_line_request* request = nullptr;

    if (ls && lc && rc) {
        g_gpiod.req_set_consumer(rc, "sx1255-multimode");
        // 3 = GPIOD_LINE_DIRECTION_OUTPUT (libgpiod v2 enum).
        if (g_gpiod.settings_set_direction(ls, 3) == 0 &&
            g_gpiod.config_add_line_settings(lc, m_offsets.data(), m_requestCount, ls) == 0)
            request = g_gpiod.request_lines(chip, rc, lc);
    }

    if (ls) g_gpiod.settings_free(ls);
    if (lc) g_gpiod.config_free(lc);
    if (rc) g_gpiod.req_free(rc);

    if (!request) {
        g_gpiod.chip_close(chip);
        return false;
    }

    m_chipHandle = chip;
    m_request = request;
    return true;
}

void SX1255Gpiod::close()
{
    if (m_request && g_gpiod.loaded)
        g_gpiod.release(static_cast<gpiod_line_request*>(m_request));
    if (m_chipHandle && g_gpiod.loaded)
        g_gpiod.chip_close(static_cast<gpiod_chip*>(m_chipHandle));
    m_request = nullptr;
    m_chipHandle = nullptr;
    m_requestCount = 0;
}

bool SX1255Gpiod::setLine(Index index, bool on)
{
    if (m_lines[index] < 0)
        return true;  // not wired
    if (!m_request || !g_gpiod.loaded)
        return false;
    return g_gpiod.set_value(static_cast<gpiod_line_request*>(m_request),
                             static_cast<unsigned int>(m_lines[index]), on ? 1 : 0) == 0;
}

bool SX1255Gpiod::setReset(bool on) { return setLine(kReset, on); }
bool SX1255Gpiod::setTxEnable(bool on) { return setLine(kTx, on); }
bool SX1255Gpiod::setRxEnable(bool on) { return setLine(kRx, on); }
bool SX1255Gpiod::setPtt(bool on) { return setLine(kPtt, on); }

}  // namespace sx1255
