with open('Pascal.svg') as f:
	lines = f.readlines()

state = 1
for l in range(len(lines)):
	if 'inkscape:label="state"' in lines[l]:
		lines[l] = lines[l].replace("state", f"state-{state}")
		state += 1

output = ''.join(lines)
with open('out.svg', 'w') as f:
	f.write(output)