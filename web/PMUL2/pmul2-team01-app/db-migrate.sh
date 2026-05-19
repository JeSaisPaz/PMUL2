#!/bin/sh
# Migration initiale Prisma — a executer UNE fois
# Cree le dossier prisma/migrations et applique le schema
npx prisma migrate dev --name init
