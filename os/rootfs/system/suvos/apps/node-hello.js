#!/usr/bin/node

const lang = (process.env.SUVOS_LANG || "ru").split("_")[0];

console.log(lang === "en" ? "Hello from Node.js inside SuvOS." : "Привет из Node.js внутри SuvOS.");
console.log(`Node.js: ${process.version}`);
