const fs = require('fs');
const path = require('path');

// Create minimal valid PNG files as placeholders
// PNG signature + minimal IHDR chunk for a 1x1 transparent pixel
function createMinimalPNG(width, height) {
  // This is a base64 encoded minimal PNG that we'll modify
  // For now, just create a simple colored square using a basic PNG
  const size = Math.max(width, height);
  
  // Create a simple SVG and note that real PNGs should be generated
  const svg = `<svg width="${width}" height="${height}" xmlns="http://www.w3.org/2000/svg">
    <rect width="${width}" height="${height}" fill="#0F131C"/>
    <text x="50%" y="50%" font-family="Arial" font-size="${size/10}" fill="#38BDF8" text-anchor="middle" dominant-baseline="middle">R</text>
  </svg>`;
  
  return svg;
}

const iconDir = path.join(__dirname, 'public', 'icons');
if (!fs.existsSync(iconDir)) {
  fs.mkdirSync(iconDir, { recursive: true });
}

// Create SVG placeholders that browsers can use
[192, 512].forEach(size => {
  const svg = createMinimalPNG(size, size);
  const filename = path.join(iconDir, `icon-${size}x${size}.svg`);
  fs.writeFileSync(filename, svg);
  console.log(`Created placeholder: ${filename}`);
});

console.log('\nNote: SVG placeholders created. For production, replace with actual PNG files.');
