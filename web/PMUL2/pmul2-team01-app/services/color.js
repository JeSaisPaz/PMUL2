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

module.exports = { getColors, updateColors };
