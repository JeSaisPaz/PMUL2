#!/bin/sh
# Migration initiale Prisma
# Cree le dossier prisma/migrations et applique le schema
npx prisma migrate dev --name init
