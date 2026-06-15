import { invoke } from "@tauri-apps/api/core";

const scrim = document.getElementById("scrim") as HTMLDivElement;

export interface SshParams {
  host: string;
  port: number;
  username: string;
  password: string;
}

export interface SerialParams {
  port: string;
  baudRate: number;
  dataBits: number;
  parity: number; // 0 none, 1 odd, 2 even
  stopBits: number;
}

function closeModal() {
  scrim.classList.remove("open");
  scrim.innerHTML = "";
}

function mount(html: string): HTMLDivElement {
  scrim.innerHTML = html;
  scrim.classList.add("open");
  const modal = scrim.querySelector(".modal") as HTMLDivElement;
  // Close on scrim click (but not modal click) or Escape.
  scrim.onclick = (e) => {
    if (e.target === scrim) closeModal();
  };
  return modal;
}

// Esc key closes whatever modal is open.
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape" && scrim.classList.contains("open")) {
    e.preventDefault();
    closeModal();
  }
});

export function openSshModal(onConnect: (p: SshParams) => Promise<void>) {
  const modal = mount(`
    <div class="modal">
      <div class="modal-header">
        <span class="modal-badge">SSH</span>
        <div>
          <div class="modal-title">Nouvelle connexion</div>
          <div class="modal-sub">Connectez-vous à un équipement réseau</div>
        </div>
      </div>
      <div class="modal-sep"></div>
      <div class="field">
        <label>Hôte</label>
        <input id="f-host" type="text" placeholder="192.168.1.1" autocomplete="off" spellcheck="false" />
      </div>
      <div class="field-row">
        <div class="field" style="width:120px">
          <label>Port</label>
          <input id="f-port" type="text" value="22" />
        </div>
        <div class="field" style="flex:1">
          <label>Utilisateur</label>
          <input id="f-user" type="text" placeholder="admin" autocomplete="off" spellcheck="false" />
        </div>
      </div>
      <div class="field">
        <label>Mot de passe</label>
        <input id="f-pass" type="password" autocomplete="off" />
      </div>
      <div class="modal-error" id="m-err"></div>
      <div class="modal-btns">
        <button class="btn btn-secondary" id="m-cancel">Annuler</button>
        <button class="btn btn-primary" id="m-ok">Connecter</button>
      </div>
    </div>
  `);

  const host = modal.querySelector("#f-host") as HTMLInputElement;
  const port = modal.querySelector("#f-port") as HTMLInputElement;
  const user = modal.querySelector("#f-user") as HTMLInputElement;
  const pass = modal.querySelector("#f-pass") as HTMLInputElement;
  const err = modal.querySelector("#m-err") as HTMLDivElement;
  const ok = modal.querySelector("#m-ok") as HTMLButtonElement;
  host.focus();

  async function submit() {
    if (!host.value.trim() || !user.value.trim()) {
      err.textContent = "Hôte et utilisateur sont requis.";
      return;
    }
    ok.disabled = true;
    ok.textContent = "Connexion…";
    err.textContent = "";
    try {
      await onConnect({
        host: host.value.trim(),
        port: parseInt(port.value, 10) || 22,
        username: user.value.trim(),
        password: pass.value,
      });
      closeModal();
    } catch (e) {
      err.textContent = String(e);
      ok.disabled = false;
      ok.textContent = "Connecter";
    }
  }

  ok.onclick = submit;
  (modal.querySelector("#m-cancel") as HTMLButtonElement).onclick = closeModal;
  modal.querySelectorAll("input").forEach((i) =>
    i.addEventListener("keydown", (e) => {
      if ((e as KeyboardEvent).key === "Enter") submit();
    })
  );
}

const BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
const PARITIES = [
  { label: "None", value: 0 },
  { label: "Odd", value: 1 },
  { label: "Even", value: 2 },
];

export async function openSerialModal(onConnect: (p: SerialParams) => Promise<void>) {
  let ports: string[] = [];
  try {
    ports = await invoke<string[]>("list_serial_ports");
  } catch {
    ports = [];
  }

  const portOptions = ports.length
    ? ports.map((p) => `<option value="${p}">${p}</option>`).join("")
    : `<option value="">Aucun port détecté</option>`;

  const modal = mount(`
    <div class="modal">
      <div class="modal-header">
        <span class="modal-badge modal-badge-com">COM</span>
        <div>
          <div class="modal-title">Connexion série</div>
          <div class="modal-sub">Console équipement via port COM</div>
        </div>
      </div>
      <div class="modal-sep"></div>
      <div class="field">
        <label>Port COM</label>
        <select id="f-port">${portOptions}</select>
      </div>
      <div class="field-row">
        <div class="field" style="width:180px">
          <label>Baud rate</label>
          <select id="f-baud">
            ${BAUD_RATES.map((b) => `<option value="${b}"${b === 9600 ? " selected" : ""}>${b}</option>`).join("")}
          </select>
        </div>
        <div class="field" style="flex:1">
          <label>Data bits</label>
          <select id="f-data">
            ${[8, 7, 6, 5].map((d) => `<option value="${d}">${d}</option>`).join("")}
          </select>
        </div>
      </div>
      <div class="field-row">
        <div class="field" style="width:180px">
          <label>Parité</label>
          <select id="f-parity">
            ${PARITIES.map((p) => `<option value="${p.value}">${p.label}</option>`).join("")}
          </select>
        </div>
        <div class="field" style="flex:1">
          <label>Stop bits</label>
          <select id="f-stop">
            ${[1, 2].map((s) => `<option value="${s}">${s}</option>`).join("")}
          </select>
        </div>
      </div>
      <div class="modal-error" id="m-err"></div>
      <div class="modal-btns">
        <button class="btn btn-secondary" id="m-cancel">Annuler</button>
        <button class="btn btn-primary btn-success" id="m-ok">Connecter</button>
      </div>
    </div>
  `);

  const sel = (id: string) => modal.querySelector(id) as HTMLSelectElement;
  const portEl = sel("#f-port");
  const err = modal.querySelector("#m-err") as HTMLDivElement;
  const ok = modal.querySelector("#m-ok") as HTMLButtonElement;
  portEl.focus();

  async function submit() {
    if (!portEl.value) {
      err.textContent = "Aucun port COM disponible.";
      return;
    }
    ok.disabled = true;
    ok.textContent = "Connexion…";
    err.textContent = "";
    try {
      await onConnect({
        port: portEl.value,
        baudRate: parseInt(sel("#f-baud").value, 10),
        dataBits: parseInt(sel("#f-data").value, 10),
        parity: parseInt(sel("#f-parity").value, 10),
        stopBits: parseInt(sel("#f-stop").value, 10),
      });
      closeModal();
    } catch (e) {
      err.textContent = String(e);
      ok.disabled = false;
      ok.textContent = "Connecter";
    }
  }

  ok.onclick = submit;
  (modal.querySelector("#m-cancel") as HTMLButtonElement).onclick = closeModal;
}
