// Agent: OpenCode (Claude) - AI/TestSuite
// Seed: insere les couleurs actives dans la DB via Prisma
// Usage: node seed_colors.js

const { PrismaClient } = require("./generated/prisma");
require("dotenv").config();
const { PrismaMariaDb } = require("@prisma/adapter-mariadb");
const adapter = new PrismaMariaDb({
    host: "mysql",
    user: process.env.MYSQL_USER,
    password: process.env.MYSQL_PASSWORD,
    database: process.env.MYSQL_DATABASE,
    port: 3306,
    connectionLimit: 5,
});

const prisma = new PrismaClient({ adapter });

async function seed() {
    console.log("[SEED] Nettoyage DB...");
    await prisma.iTEM_HISTORY.deleteMany();
    await prisma.sELECTION_HISTORY.deleteMany();
    await prisma.iTEM.deleteMany();
    await prisma.rEAD_CYCLE.deleteMany();
    await prisma.oRDER_LINE.deleteMany();
    await prisma.oRDER.deleteMany();
    await prisma.cOLOR.deleteMany();

    console.log("[SEED] Insertion couleurs...");
    await prisma.cOLOR.createMany({
        data: [
            { name: "Bleu",    hex: "#0000FF", hueMin: 85,  hueMax: 105, saturationMin: 50, saturationMax: 255, valueMin: 50, valueMax: 255, status: true },
            { name: "Jaune",   hex: "#FFFF00", hueMin: 25,  hueMax: 35,  saturationMin: 50, saturationMax: 255, valueMin: 50, valueMax: 255, status: true },
            { name: "Magenta", hex: "#FF00FF", hueMin: 140, hueMax: 160, saturationMin: 50, saturationMax: 255, valueMin: 50, valueMax: 255, status: true },
        ]
    });

    const count = await prisma.cOLOR.count();
    console.log(`[SEED] ${count} couleurs inserees`);
    await prisma.$disconnect();
}

seed().catch(e => {
    console.error("[SEED] Erreur:", e.message);
    process.exit(1);
});
