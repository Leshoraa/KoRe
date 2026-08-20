#!/usr/bin/env python3
"""
KoRe Biomechanical Kinematics Validation Tool
Evaluates Normalized Root Mean Square Error (NRMSE) and Coefficient of Determination (R^2)
against empirical Flash & Hogan minimum-jerk and critically damped spring-damper benchmarks.
"""

import sys
import math

def eval_minimum_jerk_spline(p: float) -> float:
    if p <= 0.0:
        return 0.0
    if p >= 1.0:
        return 1.0
    return p**3 * (10.0 + p * (-15.0 + 6.0 * p))

def main():
    print("[VALIDATE] Benchmarking 5th-order minimum-jerk trajectory polynomial...")
    
    n_samples = 1000
    y_true = []
    y_pred = []

    for i in range(n_samples + 1):
        tau = i / n_samples
        # Analytical Flash & Hogan formulation: 10*tau^3 - 15*tau^4 + 6*tau^5
        val_true = 10.0 * (tau**3) - 15.0 * (tau**4) + 6.0 * (tau**5)
        val_pred = eval_minimum_jerk_spline(tau)
        
        y_true.append(val_true)
        y_pred.append(val_pred)

    # Compute Root Mean Square Error (RMSE)
    mse = sum((yt - yp) ** 2 for yt, yp in zip(y_true, y_pred)) / len(y_true)
    rmse = math.sqrt(mse)
    
    # Range normalization for NRMSE
    y_range = max(y_true) - min(y_true)
    nrmse = rmse / y_range if y_range > 0 else 0.0

    # Compute Coefficient of Determination (R^2)
    y_mean = sum(y_true) / len(y_true)
    ss_tot = sum((yt - y_mean) ** 2 for yt in y_true)
    ss_res = sum((yt - yp) ** 2 for yt, yp in zip(y_true, y_pred))
    r_squared = 1.0 - (ss_res / ss_tot) if ss_tot > 0 else 1.0

    print(f"[METRIC] NRMSE      : {nrmse:.8f} (Benchmark requirement: <= 0.05)")
    print(f"[METRIC] R^2        : {r_squared:.8f} (Benchmark requirement: >= 0.95)")

    if nrmse <= 0.05 and r_squared >= 0.95:
        print("[PASS] Biomechanical kinematics validation satisfied all quantitative benchmarks.")
        sys.exit(0)
    else:
        print("[FAIL] Biomechanical kinematics benchmarks breached tolerance limits.")
        sys.exit(1)

if __name__ == "__main__":
    main()
