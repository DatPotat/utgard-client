#include "paclogic.h"

int paclogic_any(size_t count, int (*evaluate)(size_t, void *), void *context, int *had_error)
{
    size_t i; int routed = 0, failed = 0;
    for (i = 0; i < count; i++) {
        int one = evaluate(i, context);
        if (one < 0) failed = 1;
        else if (one > 0) { routed = 1; break; }
    }
    if (had_error) *had_error = failed;
    return routed;
}

paclogic_result paclogic_route(int received_domain, const char *original,
                               const char (*names)[DNS_NAME_SIZE], int name_count,
                               paclogic_evaluator evaluate, void *context)
{
    paclogic_result result = { 0, 0, 0 }; int i;
    if (received_domain) {
        result.vpn = evaluate(original, 1, context) > 0;
        result.rewrite = result.vpn;
        result.rewrite_host = result.vpn ? original : 0;
        return result;
    }
    if (name_count <= 0) { result.vpn = evaluate(original, 0, context) > 0; return result; }
    for (i = 0; i < name_count; i++) if (evaluate(names[i], 1, context) > 0) result.vpn = 1;
    if (result.vpn && name_count == 1) { result.rewrite = 1; result.rewrite_host = names[0]; }
    return result;
}
