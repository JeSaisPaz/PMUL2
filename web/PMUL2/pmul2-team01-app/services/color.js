const { prisma } = require("../routes/adapter");

async function getColors() {
    return prisma.cOLOR.findMany();
}

async function updateColors(color){
    const { name, hex, hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax, status } = color;
    await prisma.cOLOR.upsert({
        where: { name },
        update: { hex, hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax, status },
        create: { name, hex, hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax, status: status ?? true }
    });
}

async function initColors(color){
    await prisma.cOLOR.createMany({
        skipDuplicates: true, //permet de ne pas créer de couleur si elles existes déja 
        data: [
            // echelle OpenCV: H(0-179), S(0-255), V(0-255)
            // orange vs brun = meme bande Hue, differencies par saturation
            { id: 1, name: 'Yellow',   hueMin: 18,  hueMax: 37,  saturationMin: 40,  saturationMax: 255, valueMin: 40,  valueMax: 255, hex: '#FFFF00', status: false },
            { id: 2, name: 'Orange',  hueMin: 5,   hueMax: 17,  saturationMin: 120, saturationMax: 255, valueMin: 110, valueMax: 255, hex: '#FF8000', status: false },
            { id: 3, name: 'Brown',    hueMin: 5,   hueMax: 17,  saturationMin: 40,  saturationMax: 119, valueMin: 40,  valueMax: 255, hex: '#8B4513', status: false },
            { id: 4, name: 'Blue',    hueMin: 85,  hueMax: 134, saturationMin: 40,  saturationMax: 255, valueMin: 40,  valueMax: 255, hex: '#0000FF', status: false },
            { id: 5, name: 'Magenta', hueMin: 135, hueMax: 179, saturationMin: 40,  saturationMax: 255, valueMin: 40,  valueMax: 255, hex: '#FF00FF', status: false },
        ]
    });
}

module.exports = { getColors, updateColors, initColors };
