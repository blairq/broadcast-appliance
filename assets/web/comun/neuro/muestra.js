// La página de muestra: canales del mezclador con niveles de mentira y el joystick.
// Los canales del mezclador, con niveles de mentira (para ver los vúmetros andando).
    const canales = ["Mic 1", "Mic 2", "Atril", "Invitado", "Música", "Video", "Master"];
    const mez = document.getElementById("mezclador");
    const faders = canales.map((n, i) => {
      const c = document.createElement("div");
      c.className = "canal";
      c.innerHTML = `<ba-fader etiqueta="${n}" valor="${i === 6 ? 0 : -6 - i}"></ba-fader>
                     <ba-boton compacto ${i === 3 ? "activo variante=\"aire\"" : ""}>M</ba-boton>`;
      mez.append(c);
      return c.querySelector("ba-fader");
    });
    let t = 0;
    setInterval(() => {
      t += 0.07;
      faders.forEach((f, i) => {
        const base = -28 + 14 * Math.sin(t * (0.8 + i * 0.13) + i) + Math.random() * 6;
        f.nivel = { rms: base, pico: base + 4 + Math.random() * 3 };
      });
    }, 66);
    document.getElementById("joy").addEventListener("mover", (e) => {
      document.getElementById("joyTexto").textContent = `x ${e.detail.x.toFixed(2)} · y ${e.detail.y.toFixed(2)}`;
    });
    document.getElementById("joy").addEventListener("soltar", () => { document.getElementById("joyTexto").textContent = "x 0.00 · y 0.00"; });
