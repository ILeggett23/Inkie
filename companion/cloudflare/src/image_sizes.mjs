/* Explicit bounded image-frame budget; absent legacy size remains1024x1024. */
export function validImageSize(size){if(typeof size!=='string'||!/^\d{3,4}x\d{3,4}$/.test(size))return false;const [w,h]=size.split('x').map(Number);return size===`${w}x${h}`&&w>=512&&h>=512&&w<=1536&&h<=1536&&w%16===0&&h%16===0&&w*h>=655360&&w*h<=1536*1024&&w<=3*h&&h<=3*w;}
