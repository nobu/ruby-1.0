�P�� = 0
while gets()
  printf("%3d: %s", $., $_)
  while sub(/\w+/, '')
    if $& != "";
      �P�� += 1
    end
  end
  if ($. >= 10); break; end
end
printf("line: %d\n", $.)
printf("word: %d\n", �P��)
