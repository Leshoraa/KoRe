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

---

## 4. Discrete 2D Kalman Filter with Adaptive Measurement Noise

- **State Vector:** $\mathbf{x}_k = [x, y, v_x, v_y]^T$
- **State Transition Matrix:**

$$\mathbf{F} = \begin{bmatrix} 1 & 0 & \Delta t & 0 \\ 0 & 1 & 0 & \Delta t \\ 0 & 0 & 1 & 0 \\ 0 & 0 & 0 & 1 \end{bmatrix}, \quad \mathbf{H} = \begin{bmatrix} 1 & 0 & 0 & 0 \\ 0 & 1 & 0 & 0 \end{bmatrix}$$

- **Dynamic Measurement Covariance:**

$$R_k = R_0 \cdot \left(1.0 + \frac{\alpha}{\text{confidence}_k + \epsilon}\right), \quad \epsilon = 10^{-6}$$

- **Joseph-Stabilized Covariance Update:**

$$\mathbf{P}_k = (\mathbf{I} - \mathbf{K}_k\mathbf{H})\mathbf{P}_k^-(\mathbf{I} - \mathbf{K}_k\mathbf{H})^T + \mathbf{K}_k\mathbf{R}_k\mathbf{K}_k^T$$
