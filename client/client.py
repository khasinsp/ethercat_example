import socket
import time
from collections import deque
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# UDP-Parameter
UDP_IP = "127.0.0.1"
UDP_PORT = 6008

# UDP-Socket erstellen und binden
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.setblocking(False)  # Nicht-blockierender Socket

# Datenspeicher (deques für die letzten N Messwerte)
maxlen = 10000  # Anzahl der anzuzeigenden Datenpunkte
time_data = deque(maxlen=maxlen)
fx_data = deque(maxlen=maxlen)
fy_data = deque(maxlen=maxlen)
fz_data = deque(maxlen=maxlen)
tx_data = deque(maxlen=maxlen)
ty_data = deque(maxlen=maxlen)
tz_data = deque(maxlen=maxlen)

start_time = time.time()

# Erstelle zwei Subplots: einen für Kräfte (Fx, Fy, Fz) und einen für Drehmomente (Tx, Ty, Tz)
fig, (ax_forces, ax_torques) = plt.subplots(2, 1, figsize=(15, 10))

# Linien für Kräfte
line_fx, = ax_forces.plot([], [], label='Fx')
line_fy, = ax_forces.plot([], [], label='Fy')
line_fz, = ax_forces.plot([], [], label='Fz')
ax_forces.set_title("Kräfte")
ax_forces.legend()
ax_forces.set_xlim(0, maxlen)
# ax_forces.set_ylim(-1000, 1000)  # Passe diese Werte ggf. an

# Linien für Drehmomente
line_tx, = ax_torques.plot([], [], label='Tx')
line_ty, = ax_torques.plot([], [], label='Ty')
line_tz, = ax_torques.plot([], [], label='Tz')
ax_torques.set_title("Drehmomente")
ax_torques.legend()
ax_torques.set_xlim(0, maxlen)
# ax_torques.set_ylim(-1000, 1000)  # Passe diese Werte ggf. an

def init():
    # Leere Initialisierung der Linien
    line_fx.set_data([], [])
    line_fy.set_data([], [])
    line_fz.set_data([], [])
    line_tx.set_data([], [])
    line_ty.set_data([], [])
    line_tz.set_data([], [])
    return line_fx, line_fy, line_fz, line_tx, line_ty, line_tz

def update(frame):
    # Versuche alle verfügbaren UDP-Daten auszulesen (nicht blockierend)
    while True:
        try:
            data, addr = sock.recvfrom(1024)

            # Empfangene Daten dekodieren, Leerzeichen und Null-Bytes entfernen
            decoded = data.decode("utf-8").strip()
            parts = decoded.split(',')
            if len(parts) >= 6:
                fx, fy, fz, tx, ty, tz = map(int, parts[:6])
                current_time = time.time() - start_time
                time_data.append(current_time)
                fx_data.append(fx)
                fy_data.append(fy)
                fz_data.append(fz)
                tx_data.append(tx)
                ty_data.append(ty)
                tz_data.append(tz)
        except BlockingIOError:
            # Keine weiteren Daten vorhanden
            break

    # Falls noch keine Daten vorliegen, wird nichts geupdated
    if not time_data:
        return line_fx, line_fy, line_fz, line_tx, line_ty, line_tz

    # Verwende Indizes als x-Achse (die letzten maxlen Messwerte)
    x = list(range(len(time_data)))
    line_fx.set_data(x, list(fx_data))
    line_fy.set_data(x, list(fy_data))
    line_fz.set_data(x, list(fz_data))
    line_tx.set_data(x, list(tx_data))
    line_ty.set_data(x, list(ty_data))
    line_tz.set_data(x, list(tz_data))

    # Automatische Achsenskalierung
    ax_forces.relim()
    ax_forces.autoscale_view()
    ax_torques.relim()
    ax_torques.autoscale_view()

    return line_fx, line_fy, line_fz, line_tx, line_ty, line_tz

ani = animation.FuncAnimation(fig, update, init_func=init, interval=100, blit=False)

plt.tight_layout()
plt.show()