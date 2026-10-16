import {Container} from '@cloudflare/containers';
export class Converter extends Container {defaultPort=8080;sleepAfter='10s';enableInternet=false;}
export default {async fetch(request,env){const id=request.headers.get('X-Job-ID')||'';if(request.method!=='POST'||new URL(request.url).pathname!=='/convert'||!/^[a-f0-9-]{36}$/.test(id))return new Response('Invalid internal conversion request',{status:400});const instance=env.CONVERTERS.getByName('bounded-pool-'+(parseInt(id.slice(0,2),16)%2));return instance.fetch(request);}};
