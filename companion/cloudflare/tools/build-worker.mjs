import {build} from 'esbuild';
await build({entryPoints:['src/worker.mjs'],bundle:true,format:'esm',platform:'browser',target:'es2022',external:['node:*','cloudflare:*'],outfile:'work/scene-build/worker.js',sourcemap:true});
console.log('LOCAL_WORKER_BUNDLE_BUILT_NO_DEPLOY_NO_ENV_FILES');
