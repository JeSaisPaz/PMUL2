import requests

BASE_URL = "http://localhost:3000/api"

def get_items():
    response = requests.get(f"{BASE_URL}/items")
    return response.json()

def get_item(id):
    items = get_items()
    return next((i for i in items if i['id'] == int(id)), None)

def update_status(id, status):
    response = requests.patch(f"{BASE_URL}/items/{id}/status",
        json={ "status": { "status": status } }
    )
    return response.status_code, response.json() if response.content else None

def main():
    id = input("Item ID: ")
    item = get_item(id)

    if not item:
        print("Item not found.")
        return

    print(f"\nItem #{item['id']}")
    print(f"  Color      : {item['COLOR']['name']}")
    print(f"  Team       : {item['team']}")
    print(f"  Decision   : {item['decision']}")
    print(f"  Status     : {item['status']}")
    print(f"  Dec. status: {item['decisionStatus']}")

    if item['decisionStatus'] != 'PROCESS':
        print("\nItem already processed.")
        return

    print("\n1. Confirm")
    print("2. Fail")
    choice = input("Choice: ")

    if choice == '1':
        code, result = update_status(id, 'CONFIRMED')
    elif choice == '2':
        code, result = update_status(id, 'FAILED')
    else:
        print("Invalid choice.")
        return

    if code in (200, 204):
        print("\nDone.")
        if result:
            print(f"Result: {result}")
    else:
        print(f"\nError {code}: {result}")

if __name__ == "__main__":
    main()