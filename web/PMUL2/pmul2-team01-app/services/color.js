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
            { id: 1, name: 'Red',     hueMin: 0,   hueMax: 10,  saturationMin: 50, saturationMax: 100, valueMin: 50, valueMax: 100, hex: '#FF0000', status: false },
            { id: 2, name: 'Orange',  hueMin: 11,  hueMax: 25,  saturationMin: 50, saturationMax: 100, valueMin: 50, valueMax: 100, hex: '#FF8000', status: false },
            { id: 3, name: 'Yellow',  hueMin: 26,  hueMax: 35,  saturationMin: 50, saturationMax: 100, valueMin: 50, valueMax: 100, hex: '#FFFF00', status: false },
            { id: 4, name: 'Green',   hueMin: 36,  hueMax: 150, saturationMin: 50, saturationMax: 100, valueMin: 50, valueMax: 100, hex: '#00FF00', status: false },
            { id: 5, name: 'Blue',    hueMin: 151, hueMax: 260, saturationMin: 50, saturationMax: 100, valueMin: 50, valueMax: 100, hex: '#0000FF', status: false },
            { id: 6, name: 'Magenta', hueMin: 261, hueMax: 360, saturationMin: 50, saturationMax: 100, valueMin: 50, valueMax: 100, hex: '#FF00FF', status: false },
        ]
    });
}

module.exports = { getColors, updateColors, initColors };
