/* Link proof: the self-test linked freestanding for cortex-m7 (sizes only, never flashed). */
int mcu_tls_selftest(int *step, unsigned *pool_hwm, unsigned *pool_fail, unsigned *pool_after);
volatile int linkproof_result;
void amc_main(void);   /* the board startup calls amc_main() */
void amc_main(void) {
    int step; unsigned hwm, fail, after;
    linkproof_result = mcu_tls_selftest(&step, &hwm, &fail, &after) + step + (int) hwm;
    for (;;) { }
}
