const { prisma } = require("../routes/adapter");
const fs = require('fs');
const path = require('path');

const JSON_PATH = path.join(__dirname, '../colorbook/colorbook.json');

function loadColorsFromJson() {
    try {
        const data = fs.readFileSync(JSON_PATH, 'utf8');
        return JSON.parse(data);
    } catch (err) {
        console.error("Error reading JSON:", err);
        return [];
    }
}

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
    const colors = loadColorsFromJson(); // Lecture dynamique
    await prisma.cOLOR.deleteMany({});
    await prisma.cOLOR.createMany({ data: colors });
}

//Sauvegarder la base de données vers le fichier JSON
async function saveAsJson() {
    const allColors = await getColors();
    const jsonContent = JSON.stringify(allColors, null, 2);   
    fs.writeFileSync(JSON_PATH, jsonContent, 'utf8');
}

module.exports = { getColors, updateColors, initColors, saveAsJson, loadColorsFromJson };