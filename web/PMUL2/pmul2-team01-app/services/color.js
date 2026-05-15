const { prisma } = require("../routes/adapter");

async function getColors() {
    return prisma.cOLOR.findMany({
        where: { status: true }
    });
}

module.exports = { getColors };
