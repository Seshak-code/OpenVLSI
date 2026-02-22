var Module = { noExitRuntime: false };
var AcuteSim = require('./build-wasm/AcuteSim.js');
AcuteSim(Module).then(function(instance) {
  console.log("Instance initialized");
  console.log("noExitRuntime:", instance.noExitRuntime);
}).catch(console.error);
