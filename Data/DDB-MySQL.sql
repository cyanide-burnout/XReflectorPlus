CREATE TABLE Tables
(
  `Table` INTEGER NOT NULL,
  `Call1` CHAR(8) NOT NULL,
  `Call2` CHAR(8) NOT NULL,
  `Date` DATETIME NOT NULL,
  PRIMARY KEY(`Table`, `Call1`)
);

CREATE TABLE Users
(
  `Nick` VARCHAR(16) PRIMARY KEY,
  `Name` VARCHAR(16),
  `Address` VARCHAR(64),
  `Enabled` INTEGER DEFAULT 1,
  `Server` VARCHAR(16) DEFAULT '',
  `Date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE Gateways
(
  `Call` CHAR(8) NOT NULL,
  `Address` VARCHAR(64) NOT NULL,
  PRIMARY KEY(`Call`)
);

CREATE TABLE Table0
(
  `Call1` CHAR(8) NOT NULL,
  `Call2` CHAR(8) NOT NULL,
  PRIMARY KEY(`Call1`)
);

CREATE TABLE Table1
(
  `Call1` CHAR(8) NOT NULL,
  `Call2` CHAR(8) NOT NULL,
  PRIMARY KEY(`Call1`)
);

CREATE INDEX TableDate ON Tables (`Table`, `Date`);
CREATE INDEX LockExpires ON Locks (`Expires`);
CREATE INDEX GatewayAddress ON Gateways (`Address`);

CREATE INDEX Addresses ON Gateways (`Address`, `Call`);

delimiter //

CREATE TRIGGER RegisterCall AFTER INSERT ON Tables FOR EACH ROW
BEGIN
  IF (NEW.`Table` = 0) THEN
    REPLACE
      INTO Table0 (`Call1`, `Call2`)
      VALUES (NEW.`Call1`, NEW.`Call2`);
  END IF;
  IF (NEW.`Table` = 1) THEN
    REPLACE
      INTO Table1 (`Call1`, `Call2`)
      VALUES (NEW.`Call1`, NEW.`Call2`);
  END IF;
END;
//

CREATE TRIGGER RegisterUser AFTER INSERT ON Users FOR EACH ROW
BEGIN
  IF (NEW.`Enabled` = 1) AND (NEW.`Nick` LIKE '%-_') THEN
    REPLACE
      INTO Gateways (`Call`, `Address`)
      VALUES (SUBSTR(CONCAT(UPPER(NEW.`Name`), '        '), 1, 8), NEW.`Address`);
  END IF;
END;
//

delimiter ;

CREATE VIEW Routes AS
  SELECT 
      Table0.`Call1` AS `Call`,
      Table0.`Call2` AS `Repeater`,
      Table1.`Call2` AS `Gateway`,
      `Address`
    FROM Table0
    JOIN Table1 ON Table0.`Call2` = Table1.`Call1`
    JOIN Gateways ON Table1.`Call2` = Gateways.`Call`;
