#include "release_trace.h"

#if APP_UART0_ENABLE && (UART0_BOOT_LOG_ENABLE || UART0_TRANSFER_LOG_ENABLE || \
                         UART0_BLE_LOG_ENABLE || UART0_IMAGE_LOG_ENABLE || \
                         UART0_BUSY_LOG_ENABLE || UART0_OTA_LOG_ENABLE || \
                         UART0_FAULT_LOG_ENABLE)
#include "release_uart0.h"

void release_trace_write(const UINT8 *data, UINT16 length)
{
    release_uart0_write(data, length);
}

static void release_trace_nibble(UINT8 value)
{
    static const char digits[] = "0123456789ABCDEF";
    UINT8 text = (UINT8)digits[value & 0x0fU];
    release_trace_write(&text, 1);
}

void release_trace_hex8(UINT8 value)
{
    release_trace_nibble(value >> 4);
    release_trace_nibble(value);
}

void release_trace_hex32(UINT32 value)
{
    UINT8 shift;
    for(shift = 28; ; shift -= 4){
        release_trace_nibble((UINT8)(value >> shift));
        if(shift == 0) break;
    }
}
#endif

#include "img_perf.h"
#if IMG_PERF_ENABLE
/* One current receive/display session; two output sides retain separate totals. */
static struct {
    UINT32 ticks[IP_COUNT], calls[IP_COUNT];
    UINT32 begin, rx_enter, rx_last, queue_at, refresh_at, tail_at, replay_at;
    UINT32 rx_bytes, zip_bytes, spi_bytes, spi_calls, pass_spi, pass_bytes;
    struct { UINT32 ticks, spi, bytes; UINT8 side; } pass[2];
    UINT8 active, rx_open, rx_closed, queued, refreshing, seen, tail_ready;
    UINT8 pending, result, source, pixels, passes, stage;
} ip;

UINT32 IP_Now(void) { return ip.active ? RTC_GetCycle32k() : 0; }
UINT32 IP_Start(UINT8 id)
{
    if(ip.active) ip.stage = id;
    return IP_Now();
}
UINT32 IP_Diff(UINT32 end, UINT32 start)
{
    return end >= start ? end - start : RTC_MAX_COUNT - start + end;
}
static UINT32 ip_ms(UINT32 ticks)
{
    /* Avoid overflow and 64-bit division. */
    return (ticks / CAB_LSIFQ) * 1000U +
           ((ticks % CAB_LSIFQ) * 1000U) / CAB_LSIFQ;
}
UINT32 IP_Value(UINT8 id) { return ip.active ? ip.ticks[id] : 0; }
void IP_Add(UINT8 id, UINT32 ticks)
{
    if(!ip.active) return;
    /* Busy-rejected BLE writes must not pollute an ongoing display session. */
    if((id == IP_AES || id == IP_COPY || id == IP_HEADER) && !ip.rx_open) return;
    ip.ticks[id] += ticks; ip.calls[id]++;
}
void IP_Toc(UINT8 id, UINT32 start) { IP_Add(id, IP_Diff(IP_Now(), start)); }
static void ip_begin(UINT8 source)
{
    memset(&ip, 0, sizeof(ip));
    ip.active = 1; ip.source = source; ip.begin = IP_Now();
}
void IP_Stop(UINT8 status)
{
    UINT8 i;
    if(!ip.active) return;
    if(ip.rx_open) { ip.pending = status + 1U; return; }
    IP_Toc(IP_TOTAL, ip.begin);
    ip.active = 0; ip.pixels = 0; /* Freeze before any UART output. */
    BOOT_LOG_HEX8("P result=", status);
    BOOT_LOG_HEX8("P stage=", ip.stage);
    BOOT_LOG_HEX8("P source=", ip.source); /* 0=BLE, 1=stored replay */
    BOOT_LOG_HEX32("P rxB=", ip.rx_bytes);
    BOOT_LOG_HEX32("P zipB=", ip.zip_bytes);
    BOOT_LOG_HEX32("SPI n=", ip.spi_calls);
    BOOT_LOG_HEX32("SPI bytes=", ip.spi_bytes);
    BOOT_LOG_HEX32("SPI ms=", ip_ms(ip.ticks[IP_SPI]));
    for(i = 0; i < IP_COUNT; i++) {
        if(!ip.calls[i]) continue;
        BOOT_LOG_HEX8("P id=", i);
        BOOT_LOG_HEX32("P ms=", ip_ms(ip.ticks[i]));
        BOOT_LOG_HEX32("P n=", ip.calls[i]);
    }
    for(i = 0; i < ip.passes; i++) {
        BOOT_LOG_HEX8("P side=", ip.pass[i].side);
        BOOT_LOG_HEX32("P replayMs=", ip_ms(ip.pass[i].ticks));
        BOOT_LOG_HEX32("P spiN=", ip.pass[i].spi);
        BOOT_LOG_HEX32("P spiB=", ip.pass[i].bytes);
    }
}
void IP_RxEnter(UINT8 first, UINT16 bytes)
{
    if(first) {
        if(ip.active) IP_Stop(3);
        ip_begin(0);
    }
    if(!ip.active || ip.source || ip.rx_closed) return;
    ip.rx_open = 1; ip.rx_enter = ip.rx_last = IP_Start(IP_RX_WORK);
    ip.rx_bytes += bytes;
}
void IP_RxExit(UINT8 error)
{
    if(!ip.rx_open) return;
    IP_Toc(IP_RX_WORK, ip.rx_enter);
    ip.rx_open = 0;
    if(error) IP_Stop(1);
    else if(ip.pending) IP_Stop(ip.pending - 1U);
}
void IP_RxDone(void)
{
    if(!ip.active || ip.rx_closed) return;
    IP_Add(IP_RX_SPAN, IP_Diff(ip.rx_last, ip.begin));
    ip.rx_closed = 1;
}
void IP_ZipBytes(UINT16 bytes) { if(ip.active) ip.zip_bytes += bytes; }
void IP_Queue(void)
{
    if(ip.active) { ip.queued = 1; ip.queue_at = IP_Now(); }
}
void IP_Dequeue(void)
{
    if(ip.active && ip.queued) { IP_Toc(IP_QUEUE, ip.queue_at); ip.queued = 0; }
}
void IP_ReplayBegin(UINT8 side)
{
    if(!ip.active) ip_begin(1);
    ip.replay_at = IP_Start(IP_REPLAY);
    ip.pass_spi = ip.spi_calls; ip.pass_bytes = ip.spi_bytes;
    if(ip.passes < 2) ip.pass[ip.passes].side = side;
}
void IP_ReplayEnd(void)
{
    UINT32 ticks = IP_Diff(IP_Now(), ip.replay_at);
    IP_Add(IP_REPLAY, ticks);
    if(ip.active && ip.passes < 2) {
        ip.pass[ip.passes].ticks = ticks;
        ip.pass[ip.passes].spi = ip.spi_calls - ip.pass_spi;
        ip.pass[ip.passes].bytes = ip.spi_bytes - ip.pass_bytes;
        ip.passes++;
    }
}
void IP_Pixels(UINT8 enabled) { ip.pixels = enabled; }
void IP_SpiCall(UINT16 bytes)
{
    if(ip.active && ip.pixels) { ip.spi_calls++; ip.spi_bytes += bytes; }
}
void IP_Refresh(void)
{
    if(!ip.active) return;
    /* A/B commands may precede one shared BUSY observation: measure from first. */
    if(!ip.refreshing) ip.refresh_at = IP_Start(IP_REFRESH);
    ip.refreshing = 1; ip.seen = 0;
}
void IP_BusySeen(void)
{
    if(ip.active && ip.refreshing && !ip.seen) {
        IP_Toc(IP_ACTIVE, ip.refresh_at); ip.seen = 1;
    }
}
void IP_BusyDone(UINT8 status)
{
    if(ip.active && ip.refreshing) {
        IP_Toc(IP_REFRESH, ip.refresh_at);
        ip.refreshing = 0; ip.tail_ready = 1;
        ip.tail_at = IP_Now(); ip.result = status;
    }
}
void IP_Tail(void)
{
    if(ip.active && ip.tail_ready == 1) {
        IP_Toc(IP_TAIL_WAIT, ip.tail_at); ip.tail_ready = 2;
    }
}
#endif

#if IMG_PERF_ENABLE
void IP_FinishTail(void)
{
    if(ip.active && ip.tail_ready == 2) IP_Stop(ip.result);
    else if(ip.active && !ip.rx_closed && !ip.source) IP_Stop(3);
}
void IP_Disconnect(void)
{
    /* A normal disconnect after reception must not close the refresh timer. */
    if(ip.active && !ip.source && !ip.rx_closed) IP_Stop(3);
}
#endif
