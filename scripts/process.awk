#!/usr/bin/awk -f
# process.awk: parse ns-3 STA logs, print per-STA avg/std and compute
# group-average Jain fairness between HE(11ax) and legacy(11ac) groups.
# Also prints Network throughput = sum of per-STA mean throughputs.

function add_stat(sumArr, sum2Arr, k, v) {
    sumArr[k]  += v
    sum2Arr[k] += v * v
}

function mean(sum, n) { return (n > 0 ? sum / n : 0) }

function stddev(sum, sum2, n, mu, var) {
    if (n <= 0) return 0
    mu = sum / n
    var = (sum2 / n) - (mu * mu)
    if (var < 0) var = 0   # guard tiny negative due to floating error
    return sqrt(var)
}

function jain2(x1, x2, denom) {
    denom = 2 * (x1*x1 + x2*x2)
    if (denom <= 0) return 0
    return ((x1 + x2) * (x1 + x2)) / denom
}

/^STA\[/ {
    # STA index
    match($0, /STA\[([0-9]+)\]/, m)
    sta = m[1]

    # STA type
    if ($0 ~ /HT\(11ac\)/)       type[sta] = "11ac"
    else if ($0 ~ /HE\(11ax\)/) type[sta] = "11ax"
    else                        type[sta] = "unknown"

    cnt[sta]++

    # throughput
    match($0, /throughput=([0-9.]+)/, m); v = m[1] + 0
    add_stat(thr_sum, thr_sum2, sta, v)

    # avgMacQueue
    match($0, /avgMacQueue=([0-9.]+)/, m); v = m[1] + 0
    add_stat(q_sum, q_sum2, sta, v)

    # macTxDataFailed
    match($0, /macTxDataFailed=([0-9]+)/, m); v = m[1] + 0
    add_stat(fail_sum, fail_sum2, sta, v)

    # heSuTxMpdu
    match($0, /heSuTxMpdu=([0-9]+)/, m); v = m[1] + 0
    add_stat(su_sum, su_sum2, sta, v)

    # heTbTxMpdu
    match($0, /heTbTxMpdu=([0-9]+)/, m); v = m[1] + 0
    add_stat(tb_sum, tb_sum2, sta, v)
}

END {
    printf("%-15s %-18s %-18s %-18s %-18s %-18s\n",
           "STA",
           "throughput(avg/std)",
           "queue(avg/std)",
           "txFail(avg/std)",
           "heSU(avg/std)",
           "heTB(avg/std)")

    # Sort by STA index
    n = asorti(cnt, idx)

    # For group-average Jain based on per-STA mean throughput
    n11ax = n11ac = 0
    sum_thr_avg_11ax = sum_thr_avg_11ac = 0

    # Network throughput = sum of per-STA mean throughputs
    net_thr = 0

    for (i = 1; i <= n; i++) {
        sta = idx[i]
        c = cnt[sta]

        thr_avg = mean(thr_sum[sta], c)
        q_avg   = mean(q_sum[sta], c)
        fail_avg= mean(fail_sum[sta], c)
        su_avg  = mean(su_sum[sta], c)
        tb_avg  = mean(tb_sum[sta], c)

        thr_sd  = stddev(thr_sum[sta],  thr_sum2[sta],  c)
        q_sd    = stddev(q_sum[sta],    q_sum2[sta],    c)
        fail_sd = stddev(fail_sum[sta], fail_sum2[sta], c)
        su_sd   = stddev(su_sum[sta],   su_sum2[sta],   c)
        tb_sd   = stddev(tb_sum[sta],   tb_sum2[sta],   c)

        printf("STA[%d][%s] %9.3f/%7.3f  %11.1f/%9.1f  %9.2f/%7.2f  %9.2f/%7.2f  %9.2f/%7.2f\n",
               sta, type[sta],
               thr_avg, thr_sd,
               q_avg,   q_sd,
               fail_avg, fail_sd,
               su_avg,  su_sd,
               tb_avg,  tb_sd)

        # Sum network throughput (sum of per-STA mean throughputs)
        net_thr += thr_avg

        # Accumulate per-STA mean throughput by group
        if (type[sta] == "11ax") {
            n11ax++
            sum_thr_avg_11ax += thr_avg
        } else if (type[sta] == "11ac") {
            n11ac++
            sum_thr_avg_11ac += thr_avg
        }
    }

    # Compute group means (average throughput per station in each group)
    thr_mean_11ax = (n11ax > 0 ? sum_thr_avg_11ax / n11ax : 0)
    thr_mean_11ac  = (n11ac  > 0 ? sum_thr_avg_11ac  / n11ac  : 0)

    j_group = jain2(thr_mean_11ax, thr_mean_11ac)

    printf("\n")
    printf("Network throughput (sum of per-STA mean) = %.6f Mbps\n", net_thr)
    printf("Group-average throughput (per-station mean): HE(11ax)=%.6f Mbps, Legacy(11ac)=%.6f Mbps\n",
           thr_mean_11ax, thr_mean_11ac)
    printf("Jain_group (HE vs Legacy) = %.6f\n", j_group)
}
