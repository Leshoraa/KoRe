# KoRe Mathematical and Biomechanical Modeling Specifications

## 1. Ocular Dynamics: Second-Order Critically Damped System

The biomechanical human eyeball behaves as a mass-spring-damper system driven by antagonist extraocular muscle pairs:

$$\ddot{\theta}(t) + 2\zeta\omega_n \dot{\theta}(t) + \omega_n^2 (\theta(t) - \theta_{\text{target}}) = 0$$

- **Natural Frequency:** $\omega_n = 38.0\text{ rad/s}$ (calibrated against empirical human ocular motor bandwidth).
- **Damping Ratio:** $\zeta = 1.00$ (Critical damping: eliminates visual overshoot and resonant oscillation).
- **Discrete Semi-Implicit Integration:**

$$a_k = \omega_n^2 (\theta_{\text{target}} - \theta_k) - 2\zeta\omega_n v_k$$
$$v_{k+1} = v_k + a_k \cdot \Delta t$$
$$\theta_{k+1} = \theta_k + v_{k+1} \cdot \Delta t$$

---

## 2. Saccadic Trajectory: Fifth-Order Minimum-Jerk Spline

To model natural ballistic eye movements minimizing cerebral motor jerk (Flash & Hogan formulation):

$$\theta(\tau) = \theta_0 + (\theta_{\text{target}} - \theta_0)(10\tau^3 - 15\tau^4 + 6\tau^5), \quad \tau = \frac{t - t_0}{D} \in [0, 1]$$

$$\dot{\theta}(\tau) = \frac{\theta_{\text{target}} - \theta_0}{D}(30\tau^2 - 60\tau^3 + 30\tau^4)$$

- **Main-Sequence Duration Law:**

$$D = D_0 + k \cdot |\Delta\theta| \quad (D_0 = 20\text{ ms}, \ k = 2.5\text{ ms/degree})$$

---

## 3. Affective Dynamics: 2D Russell Circumplex Stochastic Diffusion

The emotional state vector $\mathbf{e}_t = [V_t, A_t]^T$ evolves via a continuous-time Langevin stochastic differential equation:

$$d\mathbf{e}_t = -\mathbf{\Gamma} (\mathbf{e}_t - \mathbf{e}_{\text{baseline}}) dt + \mathbf{\Sigma} d\mathbf{W}_t + \mathbf{K}_{\text{stimulus}} \mathbf{u}_t$$

- $\mathbf{\Gamma} = \text{diag}(\gamma_V, \gamma_A)$: Emotional homeostatic decay rates ($\tau_v = 6.0\text{s}, \tau_a = 4.5\text{s}$).
- $\mathbf{\Sigma} d\mathbf{W}_t$: Microscopic stochastic Langevin drift representing spontaneous biological mood drift.
- $\mathbf{K}_{\text{stimulus}} \mathbf{u}_t$: Transient response driven by visual tracking confidence and proximity.

**Discrete Euler-Maruyama Numerical Update:**

$$V_{k+1} = V_k - \frac{V_k - V_0}{\tau_v}\Delta t + \sigma_v \sqrt{\Delta t} \cdot \xi_V + K_v u_k$$

$$A_{k+1} = A_k - \frac{A_k - A_0}{\tau_a}\Delta t + \sigma_a \sqrt{\Delta t} \cdot \xi_A + K_a u_k$$

Where $\xi_V, \xi_A \sim \mathcal{N}(0, 1)$ are standard normal Gaussian perturbations.

---

## 4. Discrete 2D Kalman Filter with Adaptive Measurement Noise

- **State Vector:** $\mathbf{x}_k = [x, y, v_x, v_y]^T$
- **State Transition Matrix:**

$$\mathbf{F} = \begin{bmatrix} 1 & 0 & \Delta t & 0 \\ 0 & 1 & 0 & \Delta t \\ 0 & 0 & 1 & 0 \\ 0 & 0 & 0 & 1 \end{bmatrix}, \quad \mathbf{H} = \begin{bmatrix} 1 & 0 & 0 & 0 \\ 0 & 1 & 0 & 0 \end{bmatrix}$$

- **Dynamic Measurement Covariance:**

$$R_k = R_0 \cdot \left(1.0 + \frac{\alpha}{\text{confidence}_k + \epsilon}\right), \quad \epsilon = 10^{-6}$$

- **Joseph-Stabilized Covariance Update:**

$$\mathbf{P}_k = (\mathbf{I} - \mathbf{K}_k\mathbf{H})\mathbf{P}_k^-(\mathbf{I} - \mathbf{K}_k\mathbf{H})^T + \mathbf{K}_k\mathbf{R}_k\mathbf{K}_k^T$$

---

## 5. Numerical Benchmarks and Verification Metrics

- **Normalized Root Mean Square Error:** $\text{NRMSE} \le 0.05$ against empirical Flash & Hogan trajectory data.
- **Coefficient of Determination:** $R^2 \ge 0.95$ across the normalized temporal domain $\tau \in [0, 1]$.
- **Continuous Lyapunov Stability:** All dynamic matrix eigenvalues satisfy $\text{Re}(\lambda_i) < 0$.
- **Automated Host Test Suite:** Validated via 4 C++ unit test modules (`test_affective_langevin`, `test_kalman_convergence`, `test_kinematics_feedforward`, `test_minimum_jerk`) and Python numerical validator `scripts/validate_kinematics.py`.
