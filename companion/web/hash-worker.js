import {hashBlob} from './sha256-stream.js';
self.onmessage=async({data})=>{try{const sha256=await hashBlob(data.file,bytes=>self.postMessage({bytes}));self.postMessage({sha256});}catch{self.postMessage({error:'Could not checksum this file'});}};
