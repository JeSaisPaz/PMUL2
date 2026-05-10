var express = require('express');
var router = express.Router();


const { PrismaClient, ORDER_STATUS, ITEM_STATUS, DECISION, TEAM } = require("../generated/prisma");


const { PrismaMariaDb } = require ("@prisma/adapter-mariadb");
//Temporaire
const adapter = new PrismaMariaDb({
  host: "192.168.1.167",
  user: "team01",
  password: "team01-thebestone",
  database: "team01-database",
  //pas oublier de changer le port plustard 3307-> 3306
  port: 3307,
  connectionLimit: 5,
});

const prisma = new PrismaClient({ adapter });

// /health OK
router.get('/health', (req, res) => {
    return res.status(200).json({
        status: "UP",
        timestamp: new Date().toISOString()
    });
});

// Initialisation du socket pour l'update du front en fonction de la db
//const http = require('http').createServer(app);

//Avoir toutes les commandes OK
router.get('/orders', async function (req, res) {
    try {
        const orders = await prisma.oRDER.findMany({
            orderBy: { createdAt: 'desc' }
        });
        res.json(orders);
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: error.message });
    }
});

//Delete une commande OK
router.delete('/orders/:id/delete', async function (req, res) {
    try {
        await prisma.oRDER.delete({ 
            where: { id: parseInt(req.params.id) } 
        });
        res.sendStatus(204);
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: "Deletion error" });
    }
});

//Créer une commande OK
router.post('/neworder', async function (req, res) {
    try{
        const { lines } = req.body
        const order = await prisma.oRDER.create({
            data: {
                ORDER_LINE: {
                    create: lines.map(line => ({
                        quantity: line.quantity, 
                        COLOR_id: line.id
                    }))
                }
            }
        })
        res.sendStatus(204);
    }catch(error){
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: "Creation error" });
    }
})

//Cancel order OK
router.patch('/orders/:id/cancel', async function (req, res) {
    try {
        const order = await prisma.oRDER.findUnique({
            where: { id: parseInt(req.params.id) },
            include: {
                ORDER_LINE: {
                    include: { ITEM: true }
                }
            }
        })
        if (!order) {
            return res.status(404).json({ error: "Order not found" })
        }

        if (order.status === "COMPLETED") {
            return res.status(400).json({ error: "Impossible to cancel a COMPLETED order" })
        }

        if (order.status === "CANCELLED") {
            return res.status(400).json({ error: "Order already CANCELLED" })
        }

        //on récupère chaque item de chaque lignes dans un seul tableau
        const allItems = order.ORDER_LINE.flatMap(line => line.ITEM);

        //transaction pour garantir l'integrité des données, si une requête échoue on annule c'est TOR
        await prisma.$transaction([
            prisma.oRDER.update({ 
                where: { id: order.id },
                data: {
                    status: ORDER_STATUS.CANCELLED
                }
            }),

            prisma.oRDER_LINE.updateMany({
                where: { ORDER_id: order.id },
                data: { status: ORDER_STATUS.CANCELLED }
            }),

            //on crée un historique cancelled pour chaques item de la commande
            prisma.iTEM_HISTORY.createMany({
                data: allItems.map(item => ({
                    ITEM_id: item.id,
                    status: ITEM_STATUS.CANCELLED,
                }))
            })  
        ])
        res.sendStatus(204);
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json(error.message);
    }
});

//Avoir des details sur une commande OK
router.get('/orders/:id/details', async function (req, res) {
    try {
        const id = parseInt(req.params.id);
        const order = await prisma.oRDER.findUnique({
            where: { id: id },
            include: {
                ORDER_LINE: {
                    include: {
                        COLOR:{
                            select: {
                                name : true,
                                hex: true
                            }
                        },
                        ITEM:{
                            select: {
                                id : true
                            }
                        }
                    }
                }
            }
        });
        res.json(order);
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: error.message });
    }
});

//Avoir tout les items OK
router.get('/items', async function (req, res) {
    try {
        const items = await prisma.iTEM.findMany({
            include: {
                COLOR:{
                    select: {
                        name : true,
                        hex: true
                    }
                },
                READ_CYCLE: {
                    select: {
                        scannedAt : true
                    }
                },
                ITEM_HISTORY: {
                    orderBy: {
                        createdAt: 'desc'
                    },
                    take: 1,
                    select: {
                        status: true
                    }
                },
                SELECTION_HISTORY: {
                    orderBy: {
                        createdAt: 'desc'
                    },
                    take: 1,
                    select: {
                        status: true
                    }
                }
            }
        });
        res.json(items);
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: error.message });
    }
});

//Créer un item en cours de dev
router.post('/newitem', async function (req, res) {
    try{
        const { item } = req.body
        const item = await prisma.iTEM.create({
            data: {
                team: item.team,
                COLOR_id: item.colorId
            },
            ITEM_HISTORY: {create: {}}
        })
        res.sendStatus(204);
    }catch(error){
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: "Creation error" });
    }
});

//Delete un item OK
router.delete('/items/:id/delete', async function (req, res) {
    try {
        await prisma.iTEM.delete({ 
            where: { id: parseInt(req.params.id) } 
        });
        res.sendStatus(204);
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: "Deletion error" });
    }
});

//Avoir toutes les couleurs (hex + name) OK
router.get('/colors', async function (req, res)  {
    try{
        const colors = await prisma.cOLOR.findMany({
        select: { 
            name: true, 
            hex: true,
            id: true
        }
        })
        res.json(colors)
    }catch(error){
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: error.message });
    }
})

//avoir tout les scans
router.get('/scans', async function (req, res) {
    const scans = await prisma.rEAD_CYCLE.findMany({
        include: {
            ITEM: {
                include: { COLOR: true }
            }
        },
        orderBy: { scannedAt: 'desc' }
    })
    res.json(scans)
})

//Scan OK
router.post('/scans', async function (req, res)  {
    try{
        //création d'un READ_CYCLE
        const { scan } = req.body
        const readCycle = await prisma.rEAD_CYCLE.create({
            data: {
                qrValue: scan.qrValue,
                hue: scan.hue,
                saturation: scan.saturation,
                value: scan.value
            }
        })

        //Vérifie si le qr Value est valide
        if(Object.values(TEAM).includes(scan.qrValue)){

            //Vérifie si la couleur existe en vérifiant les ranges
            const validColor = await prisma.cOLOR.findFirst({
                where: {
                    hueMin:        { lte: scan.hue },
                    hueMax:        { gte: scan.hue },
                    saturationMin: { lte: scan.saturation },
                    saturationMax: { gte: scan.saturation },
                    valueMin:      { lte: scan.value },
                    valueMax:      { gte: scan.value },
                }
            })

            if(validColor){
                //Vérifie si l'objet appartient a notre team 
                if(scan.qrValue === TEAM.TEAM01){
                    //récupère la première order line ayant besoin de cette couleur
                    const orderLineInNeed = await prisma.oRDER_LINE.findFirst({
                        where: {
                            COLOR_id: validColor.id,
                            ORDER: { status: ORDER_STATUS.IN_PROCESS },
                            status: ORDER_STATUS.IN_PROCESS
                        },
                        //on ajoute item pour calculer le nombre d'item à la mise à jour du status de la commande
                        include: { ITEM: true }
                    })
                    //création d'un item avec decision order ou stock selon le besoin
                    await prisma.iTEM.create({
                        data: {
                            team: scan.qrValue,
                            decision: orderLineInNeed ? "ORDER" : "STOCK",
                            COLOR_id: validColor.id,
                            READ_CYCLE_id: readCycle.id,
                            ORDER_LINE_id: orderLineInNeed ? orderLineInNeed.id : null,
                            ITEM_HISTORY: {create: {}},
                            SELECTION_HISTORY: {create: {}}
                        }
                    })
            
                    if(orderLineInNeed){

                        //on mets à jour le status de chaque lignes de la commande 
                        await prisma.oRDER_LINE.update({
                            where: { id: orderLineInNeed.id },
                            data: { 
                                status: orderLineInNeed.ITEM.length + 1 >= orderLineInNeed.quantity ? ORDER_STATUS.COMPLETED : ORDER_STATUS.IN_PROCESS
                            }
                        })

                        // on compte le nombre de lignes encore en cours
                        const pendingLines = await prisma.oRDER_LINE.count({
                            where: { 
                                ORDER_id: orderLineInNeed.ORDER_id,
                                status: ORDER_STATUS.IN_PROCESS
                            }
                        })

                        //si aucune lignes est IN_PROCESS alors on mets le status à COMPLETED
                        if (pendingLines === 0) {
                            await prisma.oRDER.update({
                                where: { id: orderLineInNeed.ORDER_id },
                                data: { 
                                    status: ORDER_STATUS.COMPLETED, 
                                    completedAt: new Date() 
                                }
                            })
                        }
                    }
                }else{
                    //crée un item avec decision pass car c'est pas notre team 
                    await prisma.iTEM.create({
                        data: {
                            team: scan.qrValue,
                            decision: DECISION.PASS,
                            COLOR_id: validColor.id,
                            READ_CYCLE_id: readCycle.id,
                            ITEM_HISTORY: {create: {}},
                            SELECTION_HISTORY: {create: {}}
                        }
                    })
                }
            }
        }

        res.sendStatus(204);
    }catch(error){
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: error.message });
    }
})

module.exports = router;
