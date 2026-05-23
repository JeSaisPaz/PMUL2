const { prisma, ORDER_STATUS, ITEM_STATUS, DECISION} = require("../routes/adapter");

let sensors = [
  { name: "SCAN",  state: 0 },
  { name: "NEXT",  state: 0 },
  { name: "STOCK", state: 0 },
  { name: "ORDER", state: 0 },
  { name: "PASS",  state: 0 },
];
let colorStats = {}; //garde en mémoire le nombre de fois qu'une couleur a été vue depuis le dernier restart

function setSensors(newSensors) {
  sensors = newSensors;
}

function resetColorStats() {//Reset couleurs vue depuis dernier restart
  colorStats = {};
}

function incrementColorStat(color) {//Increment à chaque scan 
  if (!colorStats[color.id]) {
    colorStats[color.id] = { name: color.name, hex: color.hex, count: 0 };
  }
  colorStats[color.id].count++;
}

async function getStats() {
  const [totalOrders, totalRequested, totalAcquired] = await Promise.all([ //promise all pour faire plusieurs requetes en parrallèle
    prisma.oRDER.count(),
    prisma.oRDER_LINE.aggregate({// aggregate pour faire la somme de la valeur de chaque quantity
      _sum: { quantity: true },
      where: {
        ORDER: { status: ORDER_STATUS.PROCESS },
        status: ORDER_STATUS.PROCESS,
      },
    }),
    prisma.iTEM.count({// count pour compter chaque items ORDERED
      where: {
        ORDER_LINE_id: { not: null },
        status: ITEM_STATUS.ORDERED,
      },
    }),
  ]);

  return {
    colorStats: Object.values(colorStats),
    totalOrders,
    totalRequested: totalRequested._sum.quantity ?? 0,
    totalAcquired,
    sensors
  };
}

module.exports = { getStats, resetColorStats, incrementColorStat, setSensors };