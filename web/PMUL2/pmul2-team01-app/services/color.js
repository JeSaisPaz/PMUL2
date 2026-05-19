const { prisma } = require("../routes/adapter");
const fs = require('fs');
const path = require('path');

const JSON_PATH = path.join(__dirname, '../colorbook/colorbook.json');
const colors = require(JSON_PATH);

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

async function initColors() {
    await Promise.all(
        colors.map(color =>
            prisma.cOLOR.upsert({
                where: { name: color.name },
                update: color,
                create: color,
            })
        )
    );
}

async function saveAsJson(color){
    const allColors = await getColors();
    const jsonContent = JSON.stringify(allColors, null, 2);   
    fs.writeFileSync(JSON_PATH, jsonContent, 'utf8');
}

module.exports = { getColors, updateColors, initColors, saveAsJson };
