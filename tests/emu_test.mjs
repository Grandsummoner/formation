// Runs the WebAssembly build of Formation in Node and checks it: node tests/emu_test.mjs build/emu.wasm [out-prefix]
import fs from "node:fs";
const wasm = fs.readFileSync(process.argv[2] || "build/emu.wasm");
const out = process.argv[3] || "build/emu";
const { instance } = await WebAssembly.instantiate(wasm, {});
const e = instance.exports, mem = () => new Uint8Array(e.memory.buffer);
let ok = true;
const check = (c, m) => { console.log((c ? "ok   " : "FAIL ") + m); if (!c) ok = false; };

e.emu_init();
e.emu_step(1300);
{ const t0 = Date.now(); e.emu_step(0xFFFFFFFF); check(Date.now() - t0 < 8000, "a huge (or negative) step cannot hang it"); }
const fb = () => new Uint16Array(e.memory.buffer, e.emu_fb(), 240 * 240);
const shot = (name) => {                       // RGB565 -> PPM
  const f = fb(), b = Buffer.alloc(240 * 240 * 3 + 16);
  const h = Buffer.from("P6 240 240 255\n"); h.copy(b);
  for (let i = 0; i < 57600; i++) { const c = f[i]; b[h.length + 3 * i] = ((c >> 11) & 31) * 255 / 31; b[h.length + 3 * i + 1] = ((c >> 5) & 63) * 255 / 63; b[h.length + 3 * i + 2] = (c & 31) * 255 / 31; }
  fs.writeFileSync(`${out}-${name}.ppm`, b.subarray(0, h.length + 57600 * 3));
};
const lit = () => fb().reduce((n, c) => n + (c !== 0), 0);
check(lit() > 1000, `the screen has been drawn (${lit()} lit pixels)`);

const midi = [];
const pump = () => { while (e.emu_midi_pop()) { const t = new Uint8Array(e.memory.buffer, e.emu_midi_tmp(), 3); midi.push([t[0], t[1], t[2]]); } };
const press = (id) => { e.emu_btn(id, 1); e.emu_step(20); e.emu_btn(id, 0); e.emu_step(20); };

press(12);                                      // PLAY
let peak = 0;
for (let ms = 0; ms < 3000; ms += 6) {          // like the audio callback: a few ms, then a block
  e.emu_step(6);
  const p = e.emu_audio(), a = new Int32Array(e.memory.buffer, p, 512);
  for (const v of a) peak = Math.max(peak, Math.abs(v));
}
pump();
const ons = midi.filter((m) => (m[0] & 0xF0) === 0x90 && m[2] > 0).map((m) => m[1]);
check(ons.length >= 10, `MIDI notes arrive (${ons.length} note-ons: ${ons.slice(0, 8).join(" ")} ...)`);
check(ons.every((n) => n >= 36 && n <= 84), "notes are in a sane range");
check(peak > 500 && peak < 32767, `the sound is audible and not clipping (peak ${peak})`);
shot("play");

e.emu_enc(3, 12); e.emu_step(60); shot("knob");     // KNOB2 on the home screen: the overlay
check(lit() > 1000, "a knob turn draws an overlay");
e.emu_step(1500); shot("back");
press(2); e.emu_step(200); shot("fx");               // FX page
e.emu_key(7, 1); e.emu_step(50); e.emu_key(7, 0);    // a key
const leds = new Uint8Array(e.memory.buffer, e.emu_led(), 48);
check(leds[12] === 1, "the PLAY LED is lit");

// presets survive: save to slot 4, read the store back
press(9); e.emu_key(4, 1); e.emu_step(40); e.emu_key(4, 0); e.emu_step(100);
const pre = new Uint8Array(e.memory.buffer, e.emu_pre(), 8);
check(e.emu_pre_ok(0) === 1 && pre[0] === 0x50 && pre[1] === 0x4E, "a preset was saved (PNV2 header)");
console.log(ok ? "ALL OK" : "FAILED");
process.exit(ok ? 0 : 1);
