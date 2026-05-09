var express = require('express');
var router = express.Router();


const { PrismaClient, ORDER_STATUS } = require("../generated/prisma");


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

router.get('/health', (req, res) => {
    return res.status(200).json({
        status: "UP",
        timestamp: new Date().toISOString()
    });
});


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

router.post('/neworder', async function (req, res) {
    try{
        const { lines } = req.body
        const order = await prisma.oRDER.create({
            data: {
                ORDER_LINE: {
                    create: await Promise.all(lines.map(async (line) => {
                        const color = await prisma.cOLOR.findUnique({ 
                            where: { name: line.colorName } 
                        })
                        return { quantity: line.quantity, COLOR_id: color.id }
                    }))
                }
            }
        })
        res.json(order)
    }catch(error){
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: "Creation error" });
    }
})

router.patch('/orders/:id/status', async function (req, res) {
    try {
        const validStatus = ['IN_PROCESS','COMPLETED','CANCELLED']
        if(!validStatus.includes(req.body.status)){
            return res.status(400).json({error : "Invalid status"})
        }

        await prisma.oRDER.update({ 
            where: { id: parseInt(req.params.id) },
            data: {
                status: req.body.status,
                completedAt: req.body.status === 'COMPLETED' ? new Date() : null
            }
        });
        res.sendStatus(204);
        console.log("Status updated");
    } catch (error) {
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: "Status update error" });
    }
});

router.get('/orders/:id/details', async function (req, res) {
    const id = parseInt(req.params.id);
    try {
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


router.get('/colors', async function (req, res)  {
    try{
        const colors = await prisma.cOLOR.findMany({
        select: { name: true, hex: true }
        })
        res.json(colors)
    }catch(error){
        console.error("ERREUR DB :", error.message);
        res.status(500).json({ error: error.message });
    }
})



module.exports = router;
