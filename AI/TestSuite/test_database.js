// Agent: OpenCode (Claude) - AI/TestSuite
// Test: Base de donnees

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
    allowPublicKeyRetrieval: true,
});

const prisma = new PrismaClient({ adapter });

let pass = 0, fail = 0;
function check(label, fn) {
    fn().then(() => {
        pass++;
        console.log(`  OK   ${label}`);
    }).catch(e => {
        fail++;
        console.log(`  FAIL ${label} -> ${e.message}`);
    }).finally(() => {
        // dernier check declenche le resume
    });
}

function checkSync(label, condition) {
    if (condition) { pass++; console.log(`  OK   ${label}`); }
    else           { fail++; console.log(`  FAIL ${label}`); }
}

async function run() {
    console.log("\nDB TEST\n");

    // connexion
    try {
        await prisma.$connect();
        console.log("  OK   connexion Prisma");
        pass++;
    } catch(e) {
        console.log(`  FAIL connexion -> ${e.message}`);
        fail++;
    }

    // tables existent
    const tables = ["iTEM", "oRDER", "oRDER_LINE", "rEAD_CYCLE", "cOLOR", "iTEM_HISTORY", "sELECTION_HISTORY"];
    for (const t of tables) {
        try {
            const count = await prisma[t].count();
            checkSync(`table ${t} (${count} lignes)`, count >= 0);
        } catch(e) {
            checkSync(`table ${t}`, false);
        }
    }

    // COLOR: contraintes d'unicite
    const colors = await prisma.cOLOR.findMany();
    const names = colors.map(c => c.name);
    checkSync("COLOR.name UNIQUE", new Set(names).size === names.length);
    const hexes = colors.filter(c => c.hex).map(c => c.hex);
    checkSync("COLOR.hex UNIQUE", new Set(hexes).size === hexes.length);

    // ORDER: statuts valides
    const orders = await prisma.oRDER.findMany();
    const validStatuses = ["IN_PROCESS", "COMPLETED", "CANCELLED"];
    checkSync("ORDER.status valide", orders.every(o => validStatuses.includes(o.status)));

    // ITEM: foreign keys coherentes
    const items = await prisma.iTEM.findMany({
        include: { COLOR: true, READ_CYCLE: true }
    });
    checkSync("ITEM.COLOR_id reference OK", items.every(i => i.COLOR !== null));
    // READ_CYCLE_id est optionnel (nullable)

    // ORDER_LINE: pointe vers ORDER + COLOR
    const lines = await prisma.oRDER_LINE.findMany({
        include: { ORDER: true, COLOR: true }
    });
    checkSync("ORDER_LINE.ORDER_id FK OK", lines.every(l => l.ORDER !== null));
    checkSync("ORDER_LINE.COLOR_id FK OK", lines.every(l => l.COLOR !== null));

    // ITEM_HISTORY: pointe vers ITEM
    const histories = await prisma.iTEM_HISTORY.findMany({
        include: { ITEM: true }
    });
    checkSync("ITEM_HISTORY.ITEM_id FK OK", histories.every(h => h.ITEM !== null));

    // SELECTION_HISTORY: pointe vers ITEM
    const selections = await prisma.sELECTION_HISTORY.findMany({
        include: { ITEM: true }
    });
    checkSync("SELECTION_HISTORY.ITEM_id FK OK", selections.every(s => s.ITEM !== null));

    console.log(`\n=== ${pass} OK, ${fail} FAIL ===\n`);
    await prisma.$disconnect();
    process.exit(fail > 0 ? 1 : 0);
}

run();
