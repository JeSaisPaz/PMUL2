// pont entre le backend et le driver Python
// gere la file d'attente FIFO des commandes a executer par l'Arduino
// Une pensée a Loic qui va surement cringer  aux commentaires.
var { PrismaClient, ORDER_STATUS } = require("../generated/prisma");

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

module.exports = function (io) {
    var piSocket = null;        // la socket du Pi connecte
    var currentOrderId = null;  // l'ID de la commande en train d'etre triee
    var pendingOrders = [];     // FIFO des IDs en attente (si le Pi est deja occupe)
    var dispatchedIds = new Set(); // les IDs deja envoyes au Pi (evite les doublons si le backend
                                   // a pas encore marque la commande COMPLETED quand order_done arrive)

    io.on('connection', function (socket) {
        console.log('[PIE] Un client se connecte:', socket.id);

        // le Pi, amen (le socket)
        socket.on('register_pi', function () {
            piSocket = socket;
            console.log('[PIE] Driver Pi enregistre:', socket.id);
            // on attend que le Pi nous dise qu'il est pret (arduino_ready)
            // avant de lui envoyer quoi que ce soit, pour pas overlap
            // une commande en cours si c'est une reconnexion
        });

        // le Pi nous envoie le progres de l'Arduino (combien de blocs tries)
        socket.on('order_progress', function (data) {
            console.log('[PIE] Progres recu:', JSON.stringify(data));
            io.emit('order_progress_update', data); // on forward aux pages web
        });

        // l'Arduino a fini son taf sur la commande en cours
        socket.on('order_done', async function (data) {
            console.log('[PIE] Commande finie:', JSON.stringify(data));
            if (currentOrderId) dispatchedIds.delete(currentOrderId);
            currentOrderId = null;
            io.emit('order_completed', data);
            // on vide la suite de la file si y a des trucs en attente
            trySendOrder();
        });

        // le Pi nous dit que l'Arduino est pret a recevoir
        socket.on('arduino_ready', function () {
            console.log('[PIE] Arduino dispo');
            trySendOrder();
        });

        socket.on('disconnect', function () {
            if (piSocket && socket.id === piSocket.id) {
                console.log('[PIE] Driver Pi deconnecte — on fait le menage');
                piSocket = null;
                currentOrderId = null;
                pendingOrders = [];
                dispatchedIds.clear();
            }
        });
    });

    // envoie une commande au Pi (appelee depuis api.js ET en interne)
    // si on passe un orderId et qu'on est occupe -> on le fout dans la file FIFO
    // si on est libre -> on depile la file ou on cherche dans la db
    async function trySendOrder(newOrderId) {
        // si on nous refile un ID alors qu'on est deja en train de bosser
        if (newOrderId && currentOrderId) {
            // on verifie qu'il est pas deja dans la queue (evite les doublons)
            if (pendingOrders.indexOf(newOrderId) === -1) {
                pendingOrders.push(newOrderId);
                console.log('[PIE] Commande #' + newOrderId + ' mise en file d attente (' + pendingOrders.length + ' en attente)');
            }
            return;
        }

        // si on a un nouvel ID et qu'on est libre, on le traite en priorite
        // (sinon on depile la file ou on va chercher dans la db)
        if (!piSocket) return; // pas de Pi co, on peut rien faire, big sad :(
 
        // deja occupe et pas de nouvel ID -> on touche a rien
        if (currentOrderId && !newOrderId) return;

        var orderId = newOrderId || null;

        // on depile la file FIFO jusqu'a trouver une commande encore valide
        while (!orderId && pendingOrders.length > 0) {
            var candidateId = pendingOrders.shift();
            var candidate = await prisma.oRDER.findUnique({ where: { id: candidateId } });
            if (candidate && candidate.status === ORDER_STATUS.IN_PROCESS) {
                orderId = candidateId;
            } else {
                console.log('[PIE] Commande #' + candidateId + ' plus valide (annulee?), on skip');
            }
        }

        // si toujours rien, on cherche dans la db la plus vieille IN_PROCESS
        // en excluant celles deja envoyees (evite le re-dispatch si le backend
        // a pas encore marque la commande COMPLETED)
        if (!orderId) {
            var dispatchedArr = Array.from(dispatchedIds);
            var order = await prisma.oRDER.findFirst({
                where: {
                    status: ORDER_STATUS.IN_PROCESS,
                    id: dispatchedArr.length > 0 ? { notIn: dispatchedArr } : undefined
                },
                orderBy: { createdAt: 'asc' }
            });
            orderId = order ? order.id : null;
        }

        if (!orderId) {
            console.log('[PIE] Aucune commande a traiter');
            return;
        }

        await dispatchOrder(orderId);
    }

    // balance une commande sur le socket du Pi
    async function dispatchOrder(orderId) {
        try {
            var order = await prisma.oRDER.findUnique({
                where: { id: orderId },
                include: {
                    ORDER_LINE: {
                        include: { COLOR: true }
                    }
                }
            });

            if (!order || order.status !== ORDER_STATUS.IN_PROCESS) {
                console.log('[PIE] Commande #' + orderId + ' plus valide, on passe a la suivante');
                currentOrderId = null;
                trySendOrder(); // on essaie la prochaine dans la file
                return;
            }

            currentOrderId = orderId;
            dispatchedIds.add(orderId);  // on marque comme envoyee pour pas la re-dispatch

            // on regroupe les quantites par couleur pour le format attendu par l'Arduino
            var totals = { blue: 0, yellow: 0, magenta: 0 };
            order.ORDER_LINE.forEach(function (line) {
                var name = line.COLOR.name.toLowerCase();
                if (name === 'bleu' || name === 'blue')
                    totals.blue += line.quantity;
                else if (name === 'jaune' || name === 'yellow')
                    totals.yellow += line.quantity;
                else if (name === 'magenta' || name === 'magenta')
                    totals.magenta += line.quantity;
            });

            // Remarque: le teamId est fixe a 0x01 car il s'agit de la meilleure equipe.
            var payload = {
                teamId: 0x01,
                blue: totals.blue,
                yellow: totals.yellow,
                magenta: totals.magenta,
                orderId: orderId
            };

            piSocket.emit('start_order', payload);
            console.log('[PIE] Commande #' + orderId + ' envoyee au Pi:', JSON.stringify(payload));
        } catch (err) {
            console.error('[PIE] Erreur:', err.message);
        }
    }

    return {
        trySendOrder: trySendOrder,
    };
};
