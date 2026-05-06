var express = require('express');
var router = express.Router();
const { PrismaClient } = require("../generated/prisma");
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

router.patch('/orders/:orderId/lines/:colorId/receive', async (req, res)=> {
    const {orderId, colorId} = req.params
    const {quantityReceived} = req.body

    try {
        if(!quantityRecevied || quantityReceived < 0) {
            return res.status(400).json({
                message: "La quantité recue ne peut pas etre inférieure a 0"
            });
        }

        
        
    } catch catch(e) {
        
    }
});

router.post()

module.exports = router;
