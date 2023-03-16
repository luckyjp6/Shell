# Shell
實作一個shell功能包括  
1. 執行bin資料夾下的執行檔  
    不受系統原有指令影響，只執行bin資料夾下存有的執行檔，bin資料夾下的檔案可在shell執行過程中增刪檔案，並非寫死的。
    ![image](https://user-images.githubusercontent.com/96563567/225489104-6db560b8-00c1-42d8-b1ed-40048469cf93.png)
2. 執行內建指令  
    - setenv [var name] [value]：新增或變更環境變數  
    - printenv [var name]：印出指定環境變數  
    - exit：離開shell  
2. Ordinary pipe (可支援至少1000次pipe)  
  ![image](https://user-images.githubusercontent.com/96563567/225489160-35b2cde5-fac3-4c46-a22f-7b113179e893.png)
3. Number pipe  
    - ＂|N＂代表將stdout pipe至後方第N行  
    - ＂!N＂代表將stdout和stderr pipe至後方第N行  
      
    下圖number指令開啟test.txt，為其加上行號後，其stdout被pipe到後方第二行指令，
  而cat開啟一個不存在的檔案，其stdout和stderr被pipe到後方第一行指令（此處若使用＂|1＂，只pipi stdout的話，stderr內容會直接印出），
  最後由number指令接收上方兩行指令的pipe過來的內容，加上行號後印出  
  ![image](https://user-images.githubusercontent.com/96563567/225491571-1a8bbbca-b59d-48d3-bf38-5d250932e03a.png)
4. File redirection  
  - 支援檔案寫入＂>＂功能，如帶寫入檔案已存在，將複寫檔案而非append  
  ![image](https://user-images.githubusercontent.com/96563567/225492551-137db50b-9d40-45d1-a666-d7b9ac6f775d.png)
